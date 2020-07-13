/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * vivid.cpp - Pipeline handler for the vivid capture device
 */

#include <libcamera/camera.h>
#include <libcamera/formats.h>

#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/log.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(VIVID)

class VividCameraData : public CameraData
{
public:
	VividCameraData(PipelineHandler *pipe, MediaDevice *media)
		: CameraData(pipe), media_(media), video_(nullptr)
	{
	}

	~VividCameraData()
	{
		delete video_;
	}

	int init();
	void bufferReady(FrameBuffer *buffer);

	MediaDevice *media_;
	V4L2VideoDevice *video_;
	Stream stream_;
};

class VividCameraConfiguration : public CameraConfiguration
{
public:
	VividCameraConfiguration();

	Status validate() override;
};

class PipelineHandlerVivid : public PipelineHandler
{
public:
	PipelineHandlerVivid(CameraManager *manager);

	CameraConfiguration *generateConfiguration(Camera *camera,
						   const StreamRoles &roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera) override;
	void stop(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

private:
	int processControls(VividCameraData *data, Request *request);

	VividCameraData *cameraData(const Camera *camera)
	{
		return static_cast<VividCameraData *>(
			PipelineHandler::cameraData(camera));
	}
};

VividCameraConfiguration::VividCameraConfiguration()
	: CameraConfiguration()
{
}

CameraConfiguration::Status VividCameraConfiguration::validate()
{
	Status status = Valid;

	if (config_.empty())
		return Invalid;

	/* Cap the number of entries to the available streams. */
	if (config_.size() > 1) {
		config_.resize(1);
		status = Adjusted;
	}

	StreamConfiguration &cfg = config_[0];

	/* Adjust the pixel format. */
	const std::vector<libcamera::PixelFormat> formats = cfg.formats().pixelformats();
	if (std::find(formats.begin(), formats.end(), cfg.pixelFormat) == formats.end()) {
		cfg.pixelFormat = cfg.formats().pixelformats()[0];
		LOG(VIVID, Debug) << "Adjusting format to " << cfg.pixelFormat.toString();
		status = Adjusted;
	}

	cfg.bufferCount = 4;

	return status;
}

PipelineHandlerVivid::PipelineHandlerVivid(CameraManager *manager)
	: PipelineHandler(manager)
{
}

CameraConfiguration *PipelineHandlerVivid::generateConfiguration(Camera *camera,
								 const StreamRoles &roles)
{
	CameraConfiguration *config = new VividCameraConfiguration();
	VividCameraData *data = cameraData(camera);

	if (roles.empty())
		return config;

	std::map<V4L2PixelFormat, std::vector<SizeRange>> v4l2Formats =
		data->video_->formats();
	std::map<PixelFormat, std::vector<SizeRange>> deviceFormats;
	std::transform(v4l2Formats.begin(), v4l2Formats.end(),
		       std::inserter(deviceFormats, deviceFormats.begin()),
		       [&](const decltype(v4l2Formats)::value_type &format) {
			       return decltype(deviceFormats)::value_type{
				       format.first.toPixelFormat(),
				       format.second
			       };
		       });

	StreamFormats formats(deviceFormats);
	StreamConfiguration cfg(formats);

	cfg.pixelFormat = formats::BGR888;
	cfg.size = { 1280, 720 };
	cfg.bufferCount = 4;

	config->addConfiguration(cfg);

	config->validate();

	return config;
}

int PipelineHandlerVivid::configure(Camera *camera, CameraConfiguration *config)
{
	VividCameraData *data = cameraData(camera);
	StreamConfiguration &cfg = config->at(0);
	int ret;

	V4L2DeviceFormat format = {};
	format.fourcc = data->video_->toV4L2PixelFormat(cfg.pixelFormat);
	format.size = cfg.size;

	ret = data->video_->setFormat(&format);
	if (ret)
		return ret;

	if (format.size != cfg.size ||
	    format.fourcc != data->video_->toV4L2PixelFormat(cfg.pixelFormat))
		return -EINVAL;

	cfg.setStream(&data->stream_);
	cfg.stride = format.planes[0].bpl;

	return 0;
}

int PipelineHandlerVivid::exportFrameBuffers(Camera *camera, Stream *stream,
					     std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	VividCameraData *data = cameraData(camera);
	unsigned int count = stream->configuration().bufferCount;

	return data->video_->exportBuffers(count, buffers);
}

int PipelineHandlerVivid::start(Camera *camera)
{
	VividCameraData *data = cameraData(camera);
	unsigned int count = data->stream_.configuration().bufferCount;

	int ret = data->video_->importBuffers(count);
	if (ret < 0)
		return ret;

	ret = data->video_->streamOn();
	if (ret < 0) {
		data->ipa_->stop();
		data->video_->releaseBuffers();
		return ret;
	}

	return 0;
}

void PipelineHandlerVivid::stop(Camera *camera)
{
	VividCameraData *data = cameraData(camera);
	data->video_->streamOff();
	data->video_->releaseBuffers();
}

int PipelineHandlerVivid::queueRequestDevice(Camera *camera, Request *request)
{
	return -1;
}

bool PipelineHandlerVivid::match(DeviceEnumerator *enumerator)
{
	DeviceMatch dm("vivid");
	dm.add("vivid-000-vid-cap");

	MediaDevice *media = acquireMediaDevice(enumerator, dm);
	if (!media)
		return false;

	std::unique_ptr<VividCameraData> data = std::make_unique<VividCameraData>(this, media);

	/* Locate and open the capture video node. */
	if (data->init())
		return false;

	/* Create and register the camera. */
	std::set<Stream *> streams{ &data->stream_ };
	std::shared_ptr<Camera> camera = Camera::create(this, data->video_->deviceName(), streams);
	registerCamera(std::move(camera), std::move(data));

	return true;
}

int VividCameraData::init()
{
	video_ = new V4L2VideoDevice(media_->getEntityByName("vivid-000-vid-cap"));
	if (video_->open())
		return -ENODEV;

	video_->bufferReady.connect(this, &VividCameraData::bufferReady);

	return 0;
}

void VividCameraData::bufferReady(FrameBuffer *buffer)
{
	Request *request = buffer->request();

	pipe_->completeBuffer(camera_, request, buffer);
	pipe_->completeRequest(camera_, request);
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerVivid);

} /* namespace libcamera */

/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Tomi Valkeinen <tomi.valkeinen@iki.fi>
 *
 * pymain.cpp - Python bindings
 */

#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <mutex>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <pybind11/functional.h>

#include <libcamera/libcamera.h>

namespace py = pybind11;

using namespace std;
using namespace libcamera;

static py::object ControlValueToPy(const ControlValue &cv)
{
	//assert(!cv.isArray());
	//assert(cv.numElements() == 1);

	switch (cv.type()) {
	case ControlTypeBool:
		return py::cast(cv.get<bool>());
	case ControlTypeByte:
		return py::cast(cv.get<uint8_t>());
	case ControlTypeInteger32:
		return py::cast(cv.get<int32_t>());
	case ControlTypeInteger64:
		return py::cast(cv.get<int64_t>());
	case ControlTypeFloat:
		return py::cast(cv.get<float>());
	case ControlTypeString:
		return py::cast(cv.get<string>());
	case ControlTypeRectangle:
	case ControlTypeSize:
	case ControlTypeNone:
	default:
		throw runtime_error("Unsupported ControlValue type");
	}
}

static ControlValue PyToControlValue(const py::object &ob, ControlType type)
{
	switch (type) {
	case ControlTypeBool:
		return ControlValue(ob.cast<bool>());
	case ControlTypeByte:
		return ControlValue(ob.cast<uint8_t>());
	case ControlTypeInteger32:
		return ControlValue(ob.cast<int32_t>());
	case ControlTypeInteger64:
		return ControlValue(ob.cast<int64_t>());
	case ControlTypeFloat:
		return ControlValue(ob.cast<float>());
	case ControlTypeString:
		return ControlValue(ob.cast<string>());
	case ControlTypeRectangle:
	case ControlTypeSize:
	case ControlTypeNone:
	default:
		throw runtime_error("Control type not implemented");
	}
}

struct CameraEvent
{
	shared_ptr<Camera> camera;
	Request::Status status;
	map<const Stream *, FrameBuffer *> bufmap;
	ControlList metadata;
	uint64_t cookie;
};

static int g_eventfd;
static mutex g_buflist_mutex;
static vector<CameraEvent> g_buflist;

static void handle_request_completed(Request *req)
{
	CameraEvent ev;
	ev.camera = req->camera();
	ev.status = req->status();
	ev.bufmap = req->buffers();
	ev.metadata = req->metadata();
	ev.cookie = req->cookie();

	{
		lock_guard guard(g_buflist_mutex);
		g_buflist.push_back(ev);
	}

	uint64_t v = 1;
	write(g_eventfd, &v, 8);
}

PYBIND11_MODULE(pycamera, m)
{
	py::class_<CameraEvent>(m, "CameraEvent")
		.def_readonly("camera", &CameraEvent::camera)
		.def_readonly("status", &CameraEvent::status)
		.def_readonly("buffers", &CameraEvent::bufmap)
		.def_property_readonly("metadata", [](const CameraEvent& self) {
			py::dict ret;

			for (const auto &[key, cv] : self.metadata) {
				const ControlId *id = properties::properties.at(key);
				py::object ob = ControlValueToPy(cv);

				ret[id->name().c_str()] = ob;
			}

			return ret;
		})
		.def_readonly("cookie", &CameraEvent::cookie)
	;

	py::class_<CameraManager>(m, "CameraManager")
		/*
		 * CameraManager::stop() cannot be called, as CameraManager expects all Camera
		 * instances to be released before calling stop and we can't have such requirement
		 * in python, especially as we have a keep-alive from Camera to CameraManager.
		 * So we rely on GC and the keep-alives, and call CameraManager::start() from
		 * the constructor.
		 */

		.def(py::init([]() {
			g_eventfd = eventfd(0, 0);

			auto cm = make_unique<CameraManager>();
			cm->start();
			return cm;
		}))

		.def_property_readonly("efd", [](CameraManager &) {
			return g_eventfd;
		})

		.def("get_ready_requests", [](CameraManager &) {
			vector<CameraEvent> v;

			{
				lock_guard guard(g_buflist_mutex);
				swap(v, g_buflist);
			}

			return v;
		})

		.def("get", py::overload_cast<const string &>(&CameraManager::get))
		.def("find", [](CameraManager &self, string str) {
			std::transform(str.begin(), str.end(), str.begin(), ::tolower);

			for (auto c : self.cameras()) {
				string id = c->id();

				std::transform(id.begin(), id.end(), id.begin(), ::tolower);

				if (id.find(str) != string::npos)
					return c;
			}

			return shared_ptr<Camera>();
		})
		.def_property_readonly("version", &CameraManager::version)

		// Create a list of Cameras, where each camera has a keep-alive to CameraManager
		.def_property_readonly("cameras", [](CameraManager &self) {
			py::list l;
			for (auto &c : self.cameras()) {
				py::object py_cm = py::cast(self);
				py::object py_cam = py::cast(c);
				py::detail::keep_alive_impl(py_cam, py_cm);
				l.append(py_cam);
			}
			return l;
		});

	py::class_<Camera, shared_ptr<Camera>>(m, "Camera", py::dynamic_attr())
		.def_property_readonly("id", &Camera::id)
		.def("acquire", &Camera::acquire)
		.def("release", &Camera::release)
		.def("start", [](shared_ptr<Camera> &self) {
			self->requestCompleted.connect(handle_request_completed);

			self->start();
		})

		.def("stop", [](shared_ptr<Camera> &self) {
			self->stop();

			self->requestCompleted.disconnect(handle_request_completed);
		})

		.def("__repr__", [](shared_ptr<Camera> &self) {
			return "<pycamera.Camera '" + self->id() + "'>";
		})

		// Keep the camera alive, as StreamConfiguration contains a Stream*
		.def("generateConfiguration", &Camera::generateConfiguration, py::keep_alive<0, 1>())
		.def("configure", &Camera::configure)

		// XXX created requests MUST be queued to be freed, python will not free them
		.def("createRequest", &Camera::createRequest, py::arg("cookie") = 0, py::return_value_policy::reference_internal)
		.def("queueRequest", &Camera::queueRequest)

		.def_property_readonly("streams", [](Camera &self) {
			py::set set;
			for (auto &s : self.streams()) {
				py::object py_self = py::cast(self);
				py::object py_s = py::cast(s);
				py::detail::keep_alive_impl(py_s, py_self);
				set.add(py_s);
			}
			return set;
		})

		.def_property_readonly("controls", [](Camera &self) {
			py::dict ret;

			for (const auto &[id, ci] : self.controls()) {
				ret[id->name().c_str()] = make_tuple<py::object>(ControlValueToPy(ci.min()),
										 ControlValueToPy(ci.max()),
										 ControlValueToPy(ci.def()));
			}

			return ret;
		})

		.def_property_readonly("properties", [](Camera &self) {
			py::dict ret;

			for (const auto &[key, cv] : self.properties()) {
				const ControlId *id = properties::properties.at(key);
				py::object ob = ControlValueToPy(cv);

				ret[id->name().c_str()] = ob;
			}

			return ret;
		});

	py::class_<CameraConfiguration>(m, "CameraConfiguration")
		.def("at", py::overload_cast<unsigned int>(&CameraConfiguration::at), py::return_value_policy::reference_internal)
		.def("validate", &CameraConfiguration::validate)
		.def_property_readonly("size", &CameraConfiguration::size)
		.def_property_readonly("empty", &CameraConfiguration::empty);

	py::class_<StreamConfiguration>(m, "StreamConfiguration")
		.def("toString", &StreamConfiguration::toString)
		.def_property_readonly("stream", &StreamConfiguration::stream, py::return_value_policy::reference_internal)
		.def_property(
			"size",
			[](StreamConfiguration &self) { return make_tuple(self.size.width, self.size.height); },
			[](StreamConfiguration &self, tuple<uint32_t, uint32_t> size) { self.size.width = get<0>(size); self.size.height = get<1>(size); })
		.def_property(
			"fmt",
			[](StreamConfiguration &self) { return self.pixelFormat.toString(); },
			[](StreamConfiguration &self, string fmt) { self.pixelFormat = PixelFormat::fromString(fmt); })
		.def_readwrite("stride", &StreamConfiguration::stride)
		.def_readwrite("frameSize", &StreamConfiguration::frameSize)
		.def_readwrite("bufferCount", &StreamConfiguration::bufferCount)
		.def_property_readonly("formats", &StreamConfiguration::formats, py::return_value_policy::reference_internal);
	;

	py::class_<StreamFormats>(m, "StreamFormats")
		.def_property_readonly("pixelFormats", [](StreamFormats &self) {
			vector<string> fmts;
			for (auto &fmt : self.pixelformats())
				fmts.push_back(fmt.toString());
			return fmts;
		})
		.def("sizes", [](StreamFormats &self, const string &pixelFormat) {
			auto fmt = PixelFormat::fromString(pixelFormat);
			vector<tuple<uint32_t, uint32_t>> fmts;
			for (const auto &s : self.sizes(fmt))
				fmts.push_back(make_tuple(s.width, s.height));
			return fmts;
		})
		.def("range", [](StreamFormats &self, const string &pixelFormat) {
			auto fmt = PixelFormat::fromString(pixelFormat);
			const auto &range = self.range(fmt);
			return make_tuple(make_tuple(range.hStep, range.vStep),
					  make_tuple(range.min.width, range.min.height),
					  make_tuple(range.max.width, range.max.height));
		});

	py::enum_<StreamRole>(m, "StreamRole")
		.value("StillCapture", StreamRole::StillCapture)
		.value("StillCaptureRaw", StreamRole::Raw)
		.value("VideoRecording", StreamRole::VideoRecording)
		.value("Viewfinder", StreamRole::Viewfinder);

	py::class_<FrameBufferAllocator>(m, "FrameBufferAllocator")
		.def(py::init<shared_ptr<Camera>>(), py::keep_alive<1, 2>())
		.def("allocate", &FrameBufferAllocator::allocate)
		.def("free", &FrameBufferAllocator::free)
		.def_property_readonly("allocated", &FrameBufferAllocator::allocated)
		// Create a list of FrameBuffer, where each FrameBuffer has a keep-alive to FrameBufferAllocator
		.def("buffers", [](FrameBufferAllocator &self, Stream *stream) {
			py::list l;
			for (auto &ub : self.buffers(stream)) {
				py::object py_fa = py::cast(self);
				py::object py_buf = py::cast(ub.get());
				py::detail::keep_alive_impl(py_buf, py_fa);
				l.append(py_buf);
			}
			return l;
		});

	py::class_<FrameBuffer, unique_ptr<FrameBuffer, py::nodelete>>(m, "FrameBuffer")
		// XXX who frees this
		.def(py::init([](vector<tuple<int, unsigned int>> planes, unsigned int cookie) {
			vector<FrameBuffer::Plane> v;
			for (const auto& t : planes)
				v.push_back({FileDescriptor(get<0>(t)), get<1>(t)});
			return new FrameBuffer(v, cookie);
		}))
		.def_property_readonly("metadata", &FrameBuffer::metadata, py::return_value_policy::reference_internal)
		.def("length", [](FrameBuffer &self, uint32_t idx) {
			const FrameBuffer::Plane &plane = self.planes()[idx];
			return plane.length;
		})
		.def("fd", [](FrameBuffer &self, uint32_t idx) {
			const FrameBuffer::Plane &plane = self.planes()[idx];
			return plane.fd.fd();
		})
		.def_property("cookie", &FrameBuffer::cookie, &FrameBuffer::setCookie);

	py::class_<Stream, unique_ptr<Stream, py::nodelete>>(m, "Stream")
		.def_property_readonly("configuration", &Stream::configuration);

	py::class_<Request, unique_ptr<Request, py::nodelete>>(m, "Request")
		.def("addBuffer", &Request::addBuffer)
		.def_property_readonly("status", &Request::status)
		.def_property_readonly("buffers", &Request::buffers)
		.def_property_readonly("cookie", &Request::cookie)
		.def_property_readonly("hasPendingBuffers", &Request::hasPendingBuffers)
		.def("set_control", [](Request &self, string &control, py::object value) {
			const auto &controls = self.camera()->controls();

			auto it = find_if(controls.begin(), controls.end(),
					  [&control](const auto &kvp) { return kvp.first->name() == control; });

			if (it == controls.end())
				throw runtime_error("Control not found");

			const auto &id = it->first;

			self.controls().set(id->id(), PyToControlValue(value, id->type()));
		});

	py::enum_<Request::Status>(m, "RequestStatus")
		.value("Pending", Request::RequestPending)
		.value("Complete", Request::RequestComplete)
		.value("Cancelled", Request::RequestCancelled);

	py::enum_<FrameMetadata::Status>(m, "FrameMetadataStatus")
		.value("Success", FrameMetadata::FrameSuccess)
		.value("Error", FrameMetadata::FrameError)
		.value("Cancelled", FrameMetadata::FrameCancelled);

	py::class_<FrameMetadata>(m, "FrameMetadata")
		.def_readonly("status", &FrameMetadata::status)
		.def_readonly("sequence", &FrameMetadata::sequence)
		.def_readonly("timestamp", &FrameMetadata::timestamp)
		.def_property_readonly("bytesused", [](FrameMetadata &self) {
			vector<unsigned int> v;
			v.resize(self.planes.size());
			transform(self.planes.begin(), self.planes.end(), v.begin(), [](const auto &p) { return p.bytesused; });
			return v;
		});

	py::enum_<CameraConfiguration::Status>(m, "ConfigurationStatus")
		.value("Valid", CameraConfiguration::Valid)
		.value("Adjusted", CameraConfiguration::Adjusted)
		.value("Invalid", CameraConfiguration::Invalid);
}
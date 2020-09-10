#!/usr/bin/python3

import pycamera as pycam
import time
import binascii
import argparse
import selectors
import os

parser = argparse.ArgumentParser()
parser.add_argument("-n", "--num-frames", type=int, default=10)
parser.add_argument("-c", "--print-crc", action="store_true")
parser.add_argument("-s", "--save-frames", action="store_true")
parser.add_argument("-m", "--max-cameras", type=int, default=1)
args = parser.parse_args()

cm = pycam.CameraManager()

cameras = cm.cameras

if len(cameras) == 0:
	print("No cameras")
	exit(0)

print("Cameras:")
for c in cameras:
	print("    {}".format(c.id))
	print("        Properties:", c.properties)
	print("        Controls:", c.controls)

contexts = []

for i in range(len(cameras)):
	contexts.append({ "camera": cameras[i], "id": i })
	if args.max_cameras and args.max_cameras - 1 == i:
		break

for ctx in contexts:
	ctx["camera"].acquire()

def configure_camera(ctx):
	camera = ctx["camera"]

	# Configure

	config = camera.generateConfiguration([pycam.StreamRole.Viewfinder])
	stream_config = config.at(0)

	#stream_config.size = (1920, 480)
	#stream_config.fmt = "BGR888"

	print("Cam {}: stream config {}".format(ctx["id"], stream_config.toString()))

	camera.configure(config);

	ctx["config"] = config

def alloc_buffers(ctx):
	camera = ctx["camera"]
	stream = ctx["config"].at(0).stream

	allocator = pycam.FrameBufferAllocator(camera);
	ret = allocator.allocate(stream)
	if ret < 0:
		print("Can't allocate buffers")
		exit(-1)

	allocated = len(allocator.buffers(stream))
	print("Cam {}: Allocated {} buffers for stream".format(ctx["id"], allocated))

	ctx["allocator"] = allocator

def create_requests(ctx):
	camera = ctx["camera"]
	stream = ctx["config"].at(0).stream
	buffers = ctx["allocator"].buffers(stream)

	requests = []

	b = -1

	for buffer in buffers:
		request = camera.createRequest()
		if request == None:
			print("Can't create request")
			exit(-1)

		ret = request.addBuffer(stream, buffer)
		if ret < 0:
			print("Can't set buffer for request")
			exit(-1)

		#request.set_control("Brightness", b)
		b += 0.25

		requests.append(request)

	ctx["requests"] = requests


def req_complete_cb(ctx, req):
	camera = ctx["camera"]

	print("Cam {}: Req {} Complete: {}".format(ctx["id"], ctx["reqs_completed"], req.status))

	bufs = req.buffers
	for stream, fb in bufs.items():
		meta = fb.metadata
		print("Cam {}: Buf seq {}, bytes {}".format(ctx["id"], meta.sequence, meta.bytesused))

		with fb.mmap(0) as b:
			if args.print_crc:
				crc = binascii.crc32(b)
				print("Cam {}:    CRC {:#x}".format(ctx["id"], crc))

			if args.save_frames:
				id = ctx["id"]
				num = ctx["reqs_completed"]
				filename = "frame-{}-{}.data".format(id, num)
				with open(filename, "wb") as f:
					f.write(b)
				print("Cam {}:    Saved {}".format(ctx["id"], filename))

	ctx["reqs_completed"] += 1

	if ctx["reqs_queued"] < args.num_frames:
		request = camera.createRequest()
		if request == None:
			print("Can't create request")
			exit(-1)

		for stream, fb in bufs.items():
			ret = request.addBuffer(stream, fb)
			if ret < 0:
				print("Can't set buffer for request")
				exit(-1)

		camera.queueRequest(request)
		ctx["reqs_queued"] += 1


def setup_callbacks(ctx):
	camera = ctx["camera"]

	ctx["reqs_queued"] = 0
	ctx["reqs_completed"] = 0

def queue_requests(ctx):
	camera = ctx["camera"]
	requests = ctx["requests"]

	camera.start()

	for request in requests:
		camera.queueRequest(request)
		ctx["reqs_queued"] += 1



for ctx in contexts:
	configure_camera(ctx)
	alloc_buffers(ctx)
	create_requests(ctx)
	setup_callbacks(ctx)

for ctx in contexts:
	queue_requests(ctx)


print("Processing...")

# Need to release GIL here, so that callbacks can be called
#while any(ctx["reqs_completed"] < args.num_frames for ctx in contexts):
#	pycam.sleep(0.1)

running = True

def readcam(fileobj, mask):
	global running
	data = os.read(fileobj, 8)

	reqs = cm.get_ready_requests()

	ctx = contexts[0]
	for req in reqs:
		ctx = next(ctx for ctx in contexts if ctx["camera"] == req.camera)
		req_complete_cb(ctx, req)

	running =  any(ctx["reqs_completed"] < args.num_frames for ctx in contexts)


sel = selectors.DefaultSelector()
sel.register(cm.efd, selectors.EVENT_READ, readcam)

print("Press enter to exit")

while running:
	events = sel.select()
	for key, mask in events:
		callback = key.data
		callback(key.fileobj, mask)

print("Exiting...")

for ctx in contexts:
	camera = ctx["camera"]
	camera.stop()
	camera.release()

print("Done")

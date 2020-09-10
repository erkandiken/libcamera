#!/usr/bin/python3

from simplecamera import SimpleCameraManager, SimpleCamera
import pykms
import pycamera as pycam
import time
import argparse
import selectors
import sys

card = pykms.Card()

res = pykms.ResourceManager(card)
conn = res.reserve_connector()
crtc = res.reserve_crtc(conn)
plane = res.reserve_generic_plane(crtc)
mode = conn.get_default_mode()
modeb = mode.to_blob(card)

req = pykms.AtomicReq(card)
req.add_connector(conn, crtc)
req.add_crtc(crtc, modeb)
req.commit_sync(allow_modeset = True)

class ScreenHandler:
	def __init__(self, card, crtc, plane):
		self.card = card
		self.crtc = crtc
		self.plane = plane
		self.bufqueue = []
		self.current = None
		self.next = None

	def handle_page_flip(self, frame, time):
		old = self.current
		self.current = self.next

		if len(self.bufqueue) > 0:
			self.next = self.bufqueue.pop(0)
		else:
			self.next = None

		if self.next:
			req = pykms.AtomicReq(self.card)
			req.add_plane(self.plane, fb, self.crtc, dst=(0, 0, fb.width, fb.height))
			req.commit()

		return old

	def queue(self, fb):
		if not self.next:
			self.next = fb

			req = pykms.AtomicReq(self.card)
			req.add_plane(self.plane, fb, self.crtc, dst=(0, 0, fb.width, fb.height))
			req.commit()
		else:
			self.bufqueue.append(fb)




screen = ScreenHandler(card, crtc, plane)



def handle_camera_frame(camera, stream, fb):
	screen.queue(cam_2_drm_map[fb])

cm = SimpleCameraManager()
cam = cm.find("imx219")
cam.open()

cam.format = "ARGB8888"
cam.resolution = (1920, 1080)

cam.callback = lambda stream, fb, camera=cam: handle_camera_frame(camera, stream, fb)

cam_2_drm_map = {}
drm_2_cam_map = {}

cam.xxx_config()

drmbuffers = []
stream_cfg = cam.stream_config
for fb in cam.buffers:
	w, h = stream_cfg.size
	stride = stream_cfg.stride
	drmfb = pykms.DmabufFramebuffer(card, w, h, pykms.PixelFormat.ARGB8888,
									[fb.fd(0)], [stride], [0])
	drmbuffers.append(drmfb)

	cam_2_drm_map[fb] = drmfb
	drm_2_cam_map[drmfb] = fb


cam.start()

def readdrm(fileobj, mask):
	for ev in card.read_events():
		if ev.type == pykms.DrmEventType.FLIP_COMPLETE:
			old = screen.handle_page_flip(ev.seq, ev.time)

			if old:
				fb = drm_2_cam_map[old]
				cam.queue_fb(fb)

running = True

def readkey(fileobj, mask):
	global running
	sys.stdin.readline()
	running = False

sel = selectors.DefaultSelector()
sel.register(card.fd, selectors.EVENT_READ, readdrm)
sel.register(sys.stdin, selectors.EVENT_READ, readkey)

print("Press enter to exit")

while running:
	events = sel.select()
	for key, mask in events:
		callback = key.data
		callback(key.fileobj, mask)

cam.stop()

print("Done")

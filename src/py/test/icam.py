#!/usr/bin/python3 -i

from simplecamera import SimpleCameraManager, SimpleCamera
from PyQt5 import QtCore, QtGui, QtWidgets
import pycamera as pycam
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("-c", "--cameras", type=str, default=None)
args = parser.parse_args()

format_map = {
	"YUYV": QtGui.QImage.Format.Format_RGB16,
	"BGR888": QtGui.QImage.Format.Format_RGB888,
	"MJPEG": QtGui.QImage.Format.Format_RGB888,
}


class MainWindow(QtWidgets.QWidget):
	requestDone = QtCore.pyqtSignal(pycam.Stream, pycam.FrameBuffer)

	def __init__(self, camera):
		super().__init__()

		# Use signal to handle request, so that the execution is transferred to the main thread
		self.requestDone.connect(self.handle_request)
		camera.callback = lambda stream, fb: self.requestDone.emit(stream, fb)

		camera.xxx_config()

		self.camera = camera

		self.label = QtWidgets.QLabel()

		windowLayout = QtWidgets.QHBoxLayout()
		self.setLayout(windowLayout)

		windowLayout.addWidget(self.label)

		controlsLayout = QtWidgets.QVBoxLayout()
		windowLayout.addLayout(controlsLayout)

		windowLayout.addStretch()

		group = QtWidgets.QGroupBox("Info")
		groupLayout = QtWidgets.QVBoxLayout()
		group.setLayout(groupLayout)
		controlsLayout.addWidget(group)

		lab = QtWidgets.QLabel(camera.id)
		groupLayout.addWidget(lab)

		self.frameLabel = QtWidgets.QLabel()
		groupLayout.addWidget(self.frameLabel)


		group = QtWidgets.QGroupBox("Properties")
		groupLayout = QtWidgets.QVBoxLayout()
		group.setLayout(groupLayout)
		controlsLayout.addWidget(group)

		for k, v in camera.properties.items():
			lab = QtWidgets.QLabel()
			lab.setText(k + " = " + str(v))
			groupLayout.addWidget(lab)

		group = QtWidgets.QGroupBox("Controls")
		groupLayout = QtWidgets.QVBoxLayout()
		group.setLayout(groupLayout)
		controlsLayout.addWidget(group)

		for k, (min, max, default) in camera.controls.items():
			lab = QtWidgets.QLabel()
			lab.setText("{} = {}/{}/{}".format(k, min, max, default))
			groupLayout.addWidget(lab)

		controlsLayout.addStretch()

		self.camera.start()

	def closeEvent(self, event):
		self.camera.stop()
		super().closeEvent(event)

	def handle_request(self, stream, fb):
		global format_map

		#meta = fb.metadata
		#print("Buf seq {}, bytes {}".format(meta.sequence, meta.bytesused))

		with fb.mmap(0) as b:
			cfg = stream.configuration
			qfmt = format_map[cfg.fmt]
			w, h = cfg.size
			pitch = cfg.stride
			img = QtGui.QImage(b, w, h, pitch, qfmt)
			self.label.setPixmap(QtGui.QPixmap.fromImage(img))

		self.frameLabel.setText("Queued: {}\nDone: {}".format(camera.reqs_queued, camera.reqs_completed))

		self.camera.queue_fb(fb)


app = QtWidgets.QApplication([])
cm = SimpleCameraManager()

notif = QtCore.QSocketNotifier(cm.cm.efd, QtCore.QSocketNotifier.Read)
notif.activated.connect(lambda x: cm.read_events())

if not args.cameras:
	cameras = cm.cameras
else:
	cameras = []
	for name in args.cameras.split(","):
		c = cm.find(name)
		if not c:
			print("Camera not found: ", name)
			exit(-1)
		cameras.append(c)

windows = []

i = 0
for camera in cameras:
	globals()["cam" + str(i)] = camera
	i += 1

	camera.open()

	fmts = camera.formats
	if "BGR888" in fmts:
		camera.format = "BGR888"
	elif "YUYV" in fmts:
		camera.format = "YUYV"
	else:
		raise Exception("Unsupported pixel format")

	camera.resolution = (640, 480)

	window = MainWindow(camera)
	window.setAttribute(QtCore.Qt.WA_ShowWithoutActivating)
	window.show()
	windows.append(window)

def cleanup():
	for w in windows:
		w.close()

	for camera in cameras:
		camera.close()
	print("Done")

import atexit
atexit.register(cleanup)

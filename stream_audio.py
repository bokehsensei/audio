import matplotlib
matplotlib.use('TkAgg') # do this before importing pylab
import matplotlib.pyplot as plt
import random
import struct

time_window = 90000 # samples
slide = 10000
data = []
try:
	microphone = open('/tmp/microphone', 'rb')
except IOError as e:
	print(e)
	sys.exit(1)

for x in range(time_window):
	data.append(struct.unpack('f',microphone.read(4))[0])
fig = plt.figure()
ax = fig.add_subplot(111)

line, = ax.plot(data)

def animate(*args):
	n = len(data)
	while True:
		for x in range(slide):
			data.append(struct.unpack('f',microphone.read(4))[0])
		n += 1
		line.set_data(range(n-slide, n), data[-slide:])
		ax.set_xlim(n-(slide+1), n-1)
		fig.canvas.draw()

fig.canvas.manager.window.after(100, animate)
plt.show()
microphone.close()

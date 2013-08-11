import matplotlib
matplotlib.use('TkAgg') # do this before importing pylab
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import struct
import sys

slide = 4*1024 # samples
#slide_ms = 1000*slide / 44100
slide_ms = 10
print('slide window: {0} milliseconds'.format(slide_ms))
time_window = 20*slide
samples = [float(0) for x in range(time_window)]
try:
	microphone = open('/tmp/microphone', 'rb')
except IOError as e:
	print(e)
	sys.exit(1)

unpacker = struct.Struct('{0}f'.format(slide))

def read_more_sound_samples():
	while True:
		yield unpacker.unpack_from(microphone.read(unpacker.size))

def update_plot(new_sound_samples, samples):
	samples.extend(new_sound_samples)
	samples = samples[-time_window:]
	line.set_data(range(len(samples)), samples)
	return line,

fig = plt.figure()
ax = fig.add_subplot(111)
line, = ax.plot(samples)
ani = animation.FuncAnimation(fig, update_plot, read_more_sound_samples, interval=0, fargs = (samples,), blit=True)
plt.show()
microphone.close()

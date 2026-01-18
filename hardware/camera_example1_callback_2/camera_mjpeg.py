import os
import mmap
import struct
import cv2
import numpy as np
from flask import Flask, Response

app = Flask(__name__)

METADATA_SIZE = 4 + 4 + 4 + 8  # frametype (I), width (I), height (I), size (Q)

def get_metadata():
    try:
        fd = os.open('/camera_metadata', os.O_RDONLY)
        data = mmap.mmap(fd, METADATA_SIZE, mmap.MAP_SHARED, mmap.PROT_READ)
        unpacked = struct.unpack('I I I Q', data.read(METADATA_SIZE))
        data.close()
        os.close(fd)
        frametype, width, height, size = unpacked
        return frametype, width, height, size
    except:
        return 0, 0, 0, 0

def get_frame():
    frametype, width, height, size = get_metadata()
    if size == 0:
        return b''

    try:
        fd = os.open('/camera_latest_name', os.O_RDONLY)
        data = mmap.mmap(fd, 256, mmap.MAP_SHARED, mmap.PROT_READ)
        shm_name = data.read(256).decode('utf-8').rstrip('\x00')
        data.close()
        os.close(fd)

        fd2 = os.open(shm_name, os.O_RDONLY)
        frame_data = mmap.mmap(fd2, size, mmap.MAP_SHARED, mmap.PROT_READ)
        buf = frame_data.read(size)
        frame_data.close()
        os.close(fd2)

        # Assume RGB8888 for simplicity
        if frametype == 3:  # Assuming CAMERA_FRAMETYPE_RGB8888 is 3
            img = np.frombuffer(buf, dtype=np.uint8).reshape((height, width, 4))[:, :, :3]
            img = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)
            _, jpeg = cv2.imencode('.jpg', img)
            return jpeg.tobytes()
        else:
            return b''
    except:
        return b''

def generate():
    while True:
        frame = get_frame()
        if frame:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')

@app.route('/video_feed')
def video_feed():
    return Response(generate(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, threaded=True)
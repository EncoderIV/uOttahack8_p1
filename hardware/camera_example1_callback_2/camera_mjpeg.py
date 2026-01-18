import socket
import struct
from flask import Flask, Response

app = Flask(__name__)

HOST = '0.0.0.0'
PORT = 5001

def get_frame(sock):
    try:
        # Receive JPEG size
        size_data = sock.recv(8)
        if not size_data: return None
        jpeg_size = struct.unpack('Q', size_data)[0]
        # Receive JPEG data
        jpeg_data = b''
        while len(jpeg_data) < jpeg_size:
            data = sock.recv(jpeg_size - len(jpeg_data))
            if not data: break
            jpeg_data += data
        if len(jpeg_data) != jpeg_size: return None
        return jpeg_data
    except:
        return None

def generate(sock):
    while True:
        frame = get_frame(sock)
        if frame:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')

@app.route('/video_feed')
def video_feed():
    return Response(generate(sock), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    # Listen for connection from QNX
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.bind((HOST, PORT))
    server_sock.listen(1)
    print("Waiting for QNX connection...")
    sock, addr = server_sock.accept()
    print(f"Connected to {addr}")
    app.run(host='0.0.0.0', port=5000, threaded=True)
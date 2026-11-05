# XIAO Tiny AI Camera

A trainable camera classification demo for the **Seeed Studio XIAO ESP32S3 Sense**, built with Arduino and PlatformIO. It provides a local MJPEG preview and learns up to three classes from examples captured through a browser.

## Hardware

- XIAO ESP32S3 Sense with its camera expansion board attached
- Wi-Fi antenna attached
- USB data cable for programming and power

## Build and upload

1. Install Visual Studio Code and the PlatformIO IDE extension.
2. Open this repository folder in VS Code.
3. Connect the board by USB.
4. Use PlatformIO **Build**, then **Upload**.

From a PlatformIO terminal:

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

The project pins the Espressif32 platform to version 8.0.0. The board definition enables PSRAM. Camera pin assignments target the XIAO ESP32S3 Sense, not other ESP32 camera boards.

## Connect

Join the board's Wi-Fi from the same phone or computer that will open the viewer:

- Network: `XIAO-Camera`
- Default demo password: `Camera7392!`
- Viewer: http://192.168.4.1

Choose to stay connected if the device reports no internet. USB supplies power and programming access; it does not connect your computer to the camera's Wi-Fi. The preview uses port 81 and the viewer/API use port 80.

The password above is a shared demo default, not a private credential. Change `ssid` and `password` in `src/main.cpp` before using the project beyond a personal demo. Anyone connected to this Wi-Fi can view the camera, train classes, and reset examples; there is no additional application login. Do not expose the services to the internet.

## Train and test

1. Give two or three classes descriptive names, such as Mug, Hand, and Empty scene.
2. Keep the camera and background steady. Fill much of the view with the object.
3. Tap **Capture 5 examples** for each class. Move the object slightly during collection.
4. Start with 15 examples per class; up to 30 are supported.
5. Present new poses after training to assess recognition.

Predictions begin when at least two classes each contain five examples. The optional third class becomes eligible after it has five examples. **Reset** deletes a class's examples. Examples are saved to the board's flash and survive power cycles. Class names are stored in the browser; another browser needs the same names entered in the same order.

## How it works and limitations

- Captures 320 x 240 JPEG frames and serves a continuous MJPEG preview.
- Decodes a frame and reduces it to an 8 x 8 grid of three-channel averages (192 features).
- For each trained class, averages its three nearest example distances and selects the closest class.
- Displays **Unsure** when the classes are too similar or all examples are too far away, using heuristic thresholds.
- Requests predictions roughly every 1.3 seconds plus processing and network time.

This is a small supervised appearance classifier, not a pretrained neural network or general object detector. It learns color and coarse spatial layout; lighting, background, position, and similar colors can confuse it. The displayed **separation** is a relative distance margin, not a calibrated probability of correctness. The preview and predictions share camera access. Start with one viewer; performance depends on Wi-Fi and training examples.

No cloud service is involved. Training stores feature vectors, not full photographs. Flash is intended for small training sets, not continuous recording.

## Validation performed

- Firmware built and uploaded successfully to a XIAO ESP32S3 Sense.
- On-board camera capture, feature extraction, classifier distance calculation, and filesystem read/write self-tests passed.
- MJPEG server startup was confirmed over serial.
- Viewer JavaScript passed a syntax check.

End-to-end browser streaming performance and recognition accuracy still require testing with user-collected examples. Startup checks do not establish model accuracy.

## Files

- `src/main.cpp`: camera, classifier, local Wi-Fi, browser interface, and storage
- `platformio.ini`: board and build configuration

## References

- [Seeed Studio camera guide](https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/)
- [Espressif camera driver](https://github.com/espressif/esp32-camera)

This is a separate project from the EECS 635 Lab 1 blink program. Uploading it replaces the board's running firmware; the original lab source can be uploaded again to restore blinking.

# This work is licensed under the MIT license.
# Copyright (c) 2013-2024 OpenMV LLC. All rights reserved.
# https://github.com/openmv/openmv/blob/master/LICENSE
#
# This example shows off using the genx320 event camera from Prophesee.

import sensor
import image
import time

sensor.reset()
sensor.set_pixformat(sensor.GRAYSCALE)  # Must always be grayscale.
sensor.set_framesize(sensor.B320X320)  # Must always be 320x320.
sensor.set_brightness(128)  # Leave at 128 generally (this is the default).
sensor.set_contrast(16)  # Increase to make the image pop.
sensor.set_color_palette(image.PALETTE_EVT_LIGHT)  # image.PALETTE_EVT_DARK for dark mode.

# The default frame rate is 50 FPS. You can change it between ~20 FPS and ~350 FPS.
sensor.set_framerate(50)

clock = time.clock()

while True:
    clock.tick()

    img = sensor.snapshot()
    # img.median(1) # noise cleanup.

    blobs = img.find_blobs(
        [(85, 95, -10, 10, -10, 10)], invert=True, pixels_threshold=10, area_threshold=100, merge=True
    )

    for blob in blobs:
        img.draw_rectangle(blob.rect(), color=(255, 0, 0))
        img.draw_cross(blob.cx(), blob.cy(), color=(0, 255, 0))

    print(clock.fps())

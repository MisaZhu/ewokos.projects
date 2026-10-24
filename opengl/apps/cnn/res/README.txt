CNN Letter Recognizer - Original Implementation
================================================

Inspired by: "Handwritten Letter Recognizer" by rickyyy
https://www.shadertoy.com/view/Nfy3DW

IMPORTANT: This is an ORIGINAL implementation, not a verbatim port.
All GLSL code was written from scratch. The CNN uses procedural
(hash-based) weights instead of trained weights, so letter recognition
will not be accurate, but the architecture and visualization work.

Architecture
------------
6-pass CNN implemented entirely in GLSL:
- Common: shared utilities, glyph tables, UI helpers (original)
- Buffer A: 32x32 ink canvas with mouse drawing (original)
- Buffer B: conv1 layer (1->12 feature maps), 3x3 kernels + ReLU + 2x2 max pool (original)
- Buffer C: conv2 layer (12->24 maps), residual 3x3 conv + 1x1 skip + pooling (original)
- Buffer D: FC classifier (26 classes), softmax, prediction metadata (original)
- Image: visualization of all CNN stages + final prediction (original)

Build
-----
The shader sources are already in res/. Generate and build:

  python3 tools/shader.py    # Generates gen/cnn_glsl.h
  make ARCH=aarch64 HW=virt  # CPU build for QEMU testing
  make ARCH=aarch64 HW=raspi5 PGL_G3D=yes BSP_LFLAGS="-lbsp -lvideocore -larch_bcm2712"  # GPU build

Usage
-----
Draw A-Z letters on the 32x32 canvas (left side of screen). The CNN processes
each frame and displays:
- Conv1 feature maps (12 channels)
- Conv2 feature maps (24 channels)  
- FC layer activations
- Softmax probabilities for all 26 classes
- Top prediction with confidence

Note: Recognition accuracy is limited due to procedural weights.
The visualization demonstrates the CNN architecture and data flow.

License
-------
Original code written for EwokOS opengl.
Inspired by Shadertoy Nfy3DW (CC BY-NC-SA 3.0).

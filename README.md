# Tinybma
*An optical flowmap generator from motion vectors using a Full Search Block Matching Algorithms (FSBMA) written in c++*

**Features:**
- MSE based motion compensation on the luma chanel **only** (input images are converted from full sRGB to YUV BT.601)
- Two visualization mode for computed motion vectors:
    - **HSV** mode where hue encodes orientation and value encodes normalized norm relative to the maximum displacement
    - **UV** mode where the red and green channels encode horizontal and vertical displacement
- Luma residue visualization
- Parallel processing: Block search and pixels transforms use all available cpu cores

> ℹ️ Current implementation doesn't support subpixel motion.

<details open>

<summary>Video demo</summary>

[![YT demo](http://img.youtube.com/vi/JUaUOEDbU1Y/0.jpg)](https://www.youtube.com/watch?v=JUaUOEDbU1Y)

</details>

## Build

```bash
cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build .
```

## Usage

Use `tinybma -h` to display program help.

### Example
```bash
tinybma ./example/sintel_source.png ./example/sintel_target.png ./example/flowmap_b4_m16.png -v -b 4 -m 16 -l -r -c hsv
```

Which produces the following optical flowmap and residue based on two frames from The Blender open movie project [Sintel](https://durian.blender.org/):

<p align="center">
    <img src="./example/flowmap_b4_m16.png" width="50%" /><img src="./example/flowmap_b4_m16_residue.png" width="50%" />
    1/4 optical flowmap with max axial movement of 16px (left) and its residual residual image (right)
</p>

<p align="center">
    <img src="./example/flowmap_b4_m16_luma_ref.png" width="50%" /><img src="./example/flowmap_b4_m16_luma_target.png" width="50%" />
    Luma reference frame (left) and luma target frame (right)
</p>

<p align="center">
    <img src="./example/sintel_source.png" width="50%" /><img src="./example/sintel_target.png" width="50%" />
    Original reference frame (left) and original target frame (right)
</p>


### Available colormaps

||HSV map | UV map|
|-------|------|-----------------------------------------------------------------------------------------------------------------------|
|Option | `--colormap hsv` or `-c hsv`      | `--colormap uv` or `-c uv`             |
|Result| <img src="./example/flowmap_b4_m16.png" width="100%" /> | <img src="./example/flowmap_b4_m16_uv.png" width="100%" />  |

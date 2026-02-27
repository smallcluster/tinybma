# Tinybma
An optical flowmap generator from motion vectors using block matching algorithms in c++

Current implementation do not support sub pixel motions and is implemented using a Full Search Block Matching Algorithm (FSBMA).

> ℹ️ Blocks are processed in parallel on the CPU

## Build

```bash
cmake -DCMAKE_BUILD_TYPE=Release .
cmake --build .
```

## Usage

Show manual :
```bash
tinybma -h
```

Example :
```bash
tinybma ./example/sintel_source.png ./example/sintel_target.png ./example/flowmap_b4_m16.png -v -b 4 -m 16
```

Which produces the following optical flowmap based on two frames from The Blender open movie project [Sintel](https://durian.blender.org/):
<p align="center">

<p align="center"><img src="./example/flowmap_b4_m16.png" width="50%" /><br>
Optical flowmap of 4x4 blocs with a maximum search of 16 pixels</p>

<p align="center"><img src="./example/sintel_source.png" width="50%" /><img src="./example/sintel_target.png" width="50%" /><br>Source frame on the left and target frame on the right</p>

</p>

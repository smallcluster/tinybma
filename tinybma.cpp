#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <omp.h>
#include <optional>

#define PI 3.14159265358979311600f

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

#define DEFAULT_BLOCKSIZE 16
#define DEFAULT_MAXSEARCH 48
#define DEFAULT_SUBPIXELS 1

//------------------------------------------------------------------------------------------------------------------------------
//  Utils
//------------------------------------------------------------------------------------------------------------------------------

inline int clamp(int val, int min, int max){
    if(val < min)
        return min;
    else if(val > max)
        return max;
    return val;
}
inline float clampf(float val, float min, float max){
    if(val < min)
        return min;
    else if(val > max)
        return max;
    return val;
}

//------------------------------------------------------------------------------------------------------------------------------
//  Color processing
//------------------------------------------------------------------------------------------------------------------------------
struct Color {
    union
    {
        unsigned char r;
        unsigned char y;
    };
    union
    {
        unsigned char g;
        unsigned char u;
    };
    union
    {
        unsigned char b;
        unsigned char v;
    };
};

inline int clip(int val){
    if(val < 0)
        return 0;
    else if (val > 255)
        return 255;
    return val;
}

inline float clipf(float val){
    if(val < 0)
        return 0;
    else if (val > 255)
        return 255;
    return val;
}

inline Color hsv2rgb(float H, float S, float V) {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    float h = H / 360.f;
    float s = S / 100.f;
    float v = V / 100.f;

    int i = static_cast<int>(std::floor(h * 6.f));
    float f = h * 6.f - i;
    float p = v * (1.f - s);
    float q = v * (1.f - f * s);
    float t = v * (1.f - (1.f - f) * s);

    switch (i % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    case 5: r = v, g = p, b = q; break;
    }

    return { static_cast<unsigned char>(clipf(r * 255.f)),
             static_cast<unsigned char>(clipf(g * 255.f)),
             static_cast<unsigned char>(clipf(b * 255.f)) };
}

// Adapted from https://learn.microsoft.com/fr-fr/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#converting-rgb888-to-yuv-444
inline Color srgb2bt601(unsigned char r, unsigned char g, unsigned char b) {
    unsigned char y = ( (  66 * r + 129 * g +  25 * b + 128) >> 8) +  16;
    unsigned char u = ( ( -38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
    unsigned char v = ( ( 112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
    return {y, u, v};
}

inline Color bt6012srgb(unsigned char y, unsigned char u, unsigned char v) {
    int C = y - 16;
    int D = u - 128;
    int E = v - 128;
    unsigned char r = clip(( 298 * C           + 409 * E + 128) >> 8);
    unsigned char g = clip(( 298 * C - 100 * D - 208 * E + 128) >> 8);
    unsigned char b = clip(( 298 * C + 516 * D           + 128) >> 8);
    return {r, g, b};
}

//------------------------------------------------------------------------------------------------------------------------------
//  CLI PARSING
//------------------------------------------------------------------------------------------------------------------------------

enum ColorMaps {
    HSV,
    UV
};

struct CLIArgs {
    int block_size = DEFAULT_BLOCKSIZE;
    int max_search = DEFAULT_MAXSEARCH;
    int subpixels = DEFAULT_SUBPIXELS;
    std::string input_path;
    std::string target_path;
    std::string output_path;
    bool export_luma = false;
    bool export_residue = false;
    bool export_prediction = false;
    bool verbose = false;
    ColorMaps color_map = HSV;
};

bool check_arg(const std::string& a, const std::string& full_name, const std::string& short_name){
    return a == "-"+short_name || a == "--"+full_name;
}

int parse_subpixels(const std::string& v){
    const std::string error_msg = "--subpixels (-s) must be positive number, got "+v;
    int num;
    try{
        num = std::stoi(v);
    } catch(...){
        throw std::runtime_error(error_msg);
    }
    if(num < 0)
        throw std::runtime_error(error_msg);
    return num;
}

int parse_block_size(const std::string& v){
    const std::string error_msg = "--blocksize (-b) must be a striclty positive number, got "+v;
    int num;
    try{
        num = std::stoi(v);
    } catch(...){
        throw std::runtime_error(error_msg);
    }
    if(num <= 0)
        throw std::runtime_error(error_msg);
    return num;
}

int parse_max_search(const std::string& v){
    const std::string error_msg = "--maxsearch (-m) must be a striclty positive number, got "+v;
    int num;
    try{
        num = std::stoi(v);
    } catch(...){
        throw std::runtime_error(error_msg);
    }
    if(num <= 0)
        throw std::runtime_error(error_msg);
    return num;
}

ColorMaps parse_color_map(const std::string& v){
    if(v == "hsv")
        return HSV;
    if(v == "uv")
        return UV;

    throw std::runtime_error("--colormap (-c) can only be set to 'hsv' or 'uv', got "+v);
}

CLIArgs parse_cli(int argc, char const *argv[]){
    // DO NOT ABUSE THE CLI
    if(argc > 64)
        throw std::runtime_error("Too many arguments");

    CLIArgs args;

    int path_index = 0;
    for(int i=1; i < argc; ++i){
        std::string a{argv[i]};

        // This is an optional arg
        if(a.rfind("-", 0) == 0){

            // Just display the help message
            if(check_arg(a, "help", "h"))
                throw std::runtime_error("help");
            else if(check_arg(a, "luma", "l")){
                args.export_luma = true;
                continue;
            }else if(check_arg(a, "residue", "r")){
                args.export_residue = true;
                continue;
            }else if (check_arg(a, "verbose", "v")) {
                args.verbose = true;
                continue;
            } else if (check_arg(a, "prediction", "p")){
                args.export_prediction = true;
                continue;
            }

            if(i+1 >= argc)
                throw std::runtime_error("Missing value for option "+a);
            ++i;
            std::string v{argv[i]};
            if(check_arg(a, "blocksize", "b")){
                try {
                    args.block_size = parse_block_size(v);
                } catch(...){
                    throw;
                }
            } else if(check_arg(a, "maxsearch", "m")){
                try {
                    args.max_search = parse_max_search(v);
                } catch(...){
                    throw;
                }
            } else if(check_arg(a, "colormap", "c")){
                try{
                    args.color_map = parse_color_map(v);
                }catch(...){
                    throw;
                }
            } else if(check_arg(a, "subpixels", "s")){
                try{
                    args.subpixels = parse_subpixels(v);
                }catch(...){
                    throw;
                }
            } else
                throw std::runtime_error("Unknown option "+a);
        }
        // Positional arg
        else {
            switch (path_index)
            {
            case 0:
                args.input_path = a;
                ++path_index;
                break;
            case 1:
                args.target_path = a;
                ++path_index;
                break;
            case 2:
                args.output_path = a;
                ++path_index;
                break;
            default:
                throw std::runtime_error("Too many positional arguments");
            }
        }
    }

    // check path where inputed
    if(args.input_path == "")
        throw std::runtime_error("Missing required positional argument REF_IMG");
    if(args.target_path == "")
        throw std::runtime_error("Missing required positional argument TARGET_IMG");
    if(args.output_path == "")
        throw std::runtime_error("Missing required positional argument OUTPUT_IMG");

    // Add .png extension to outputpath
    if(args.output_path.find(".png", args.output_path.length()-1-4) != args.output_path.length()-4)
        args.output_path = args.output_path+".png";


    return std::move(args);
}

//------------------------------------------------------------------------------------------------------------------------------
//  MAIN PROGRAM
//------------------------------------------------------------------------------------------------------------------------------


void display_help(){
    std::cout << "USAGE: tinybma [OPTION]... REF_IMG TARGET_IMG OUTPUT_IMG\n"
    << "\n"
    << "Generate an optical flowmap from REF_IMG to TARGET_IMG using a full search block matching algorithm.\n"
    << "\n"
    << "EXAMPLE: tinybma -b 16 -m 32 ref_start.png ref_end.png flowmap.png\n"
    << "\n"
    << "OPTIONS:\n"
    << "-h, --help               Display this help.\n"
    << "-b, --blocksize [INT]    Block size. Default=" << DEFAULT_BLOCKSIZE << "\n"
    << "-m, --maxsearch [INT]    Maximum allowed block movement in whole pixels. Default=" << DEFAULT_MAXSEARCH <<"\n"
    << "-v, --verbose            Display infos and progression. (optional)\n"
    << "-l, --luma               Export converted sRGB -> YUV 4:4:4 BT.601 's luma component of the REF_IMG and TARGET_IMG.\n"
    << "-r, --residue            Export the luma residue used to reconstruct the TARGET_IMG's luma value.\n"
    << "-c, --colormap [hsv|uv]  Choose the colormap to visualize the optical flow. Default=hsv\n"
    << "-s, --subpixels [INT]     How many allowed subpixels, set to 0 to disable subpixel motion. Default=" << DEFAULT_SUBPIXELS << "\n"
    << "-p, --prediction          Export luma prediction computed from the motion vectors.\n"
    << std::endl;
}

inline float interpolate_linear(float t, float start, float end){
    return t * end + (1.0f-t) * start;
}

inline float interpolate_bilinear(float x, float y, float top_left, float top_right, float bottom_left, float bottom_right){
    return interpolate_linear(y, interpolate_linear(x, top_left, top_right), interpolate_linear(x, bottom_left, bottom_right));
}

std::vector<unsigned char> upscaling_bilinear(const std::vector<unsigned char>& input_y, int img_width, int img_height, int scale){
    int up_width = img_width * scale;
    int up_height = img_height * scale;
    std::vector<unsigned char> up_y(up_width*up_height);

    #pragma omp parallel for collapse(2)
    for (int y = 0; y < up_height; ++y){
        for(int x = 0; x < up_width; ++x){
            // Original pixels
            if(x % scale == 0 && y % scale == 0){
                up_y[y * up_width + x] = input_y[ (y / scale) * img_width + (x/scale)];
            } else {
                // Interpolate
                unsigned char top_left = input_y[ (y / scale) * img_width + (x/scale)];
                unsigned char top_right = input_y[ (y / scale) * img_width + (x/scale + 1)];
                unsigned char bottom_left = input_y[ (y / scale + 1) * img_width + (x/scale)];
                unsigned char bottom_right = input_y[ (y / scale + 1) * img_width + (x/scale + 1)];
                float tx = static_cast<float>(x % scale) / static_cast<float>(scale);
                float ty = static_cast<float>(y % scale) / static_cast<float>(scale);
                float value = clampf(interpolate_bilinear(tx, ty, top_left, top_right, bottom_left, bottom_right), 0, 255);
                up_y[y * up_width + x] = static_cast<unsigned char>(value);
            }
        }
    }
    
    return std::move(up_y);
}

inline float block_mse(int mx, int my, int bx, int by, int block_size, const std::vector<unsigned char>&input_y, const std::vector<unsigned char>&target_y, int input_width, int input_height, int target_width, int target_height, int scale){
    float mse = 0;
    for (int ky = 0; ky < block_size; ++ky) {
        for (int kx = 0; kx < block_size; ++kx) {

            int src_x = bx * block_size + kx;
            int src_y = by * block_size + ky;
            int dest_x = bx * scale * block_size + kx + mx;
            int dest_y = by * scale * block_size + ky + my;

            // Ignore out of bounds pixels from the current bloc
            if (src_x >= input_width || src_y >= input_height)
                continue;

            // Consider out of bound pixel after moving to be full of zeros
            int e;
            if(dest_x < 0 || dest_y < 0 || dest_x >= target_width || dest_y >= target_height)
                e = 0;
            else
                e = target_y[dest_y * target_width + dest_x];

            int d = e - input_y[src_y * input_width + src_x];
            mse += static_cast<float>(d*d);
        }
    }
    return mse / static_cast<float>(block_size*block_size);
}

inline void fsbma(int bx, int by, int block_size, int max_search, std::vector<float>&mv, int mv_width, const std::vector<unsigned char>&input_y, const std::vector<unsigned char>&target_y, int input_width, int input_height, int target_width, int target_height, int scale){
    // Use current position as the initial best MSE
    // Ensures the best selected vector is (0,0) if all motions are equal.
    int best_mv_x = 0;
    int best_mv_y = 0;
    float best_mse = block_mse(best_mv_x, best_mv_y, bx, by, block_size, input_y, target_y, input_width, input_height, target_width, target_height, scale);
    // Perform lookup in target image over all possible direction
    // and keep the best one
    for (int my = -max_search*scale; my <= max_search*scale; ++my) {
        for (int mx = -max_search*scale; mx <= max_search*scale; ++mx) {
            // The no motion case is already precomputed
            if(my == 0 && mx == 0)
                continue;
            float mse = block_mse(mx, my, bx, by, block_size, input_y, target_y, input_width, input_height, target_width, target_height, scale);
            if (mse < best_mse) {
                best_mse = mse;
                best_mv_x = mx;
                best_mv_y = my;
            }
        }
    }
    // Store best motion vector
    float* v = &mv[0] + (2 * (by * mv_width + bx));
    v[0] = best_mv_x / scale;
    v[1] = best_mv_y / scale;
}

inline void update_progress(int* progress, int total, bool enabled){
    if(enabled){
        int previous = static_cast<int>(100.0f * static_cast<float>(*progress) / static_cast<float>(total));
        ++(*progress);
        int next = static_cast<int>(100.0f * static_cast<float>(*progress) / static_cast<float>(total));
        if (previous != next)
            std::cout << "Progress: " << *progress << "/" << total << " (" << next << "%)" << std::endl;
    }
}

inline Color vec2uv(float vx, float vy){
    float r =  0.5f * 255.0f * (1.0f + vx);
    float g = 0.5f * 255.0f * (1.0f + vy);
    return {static_cast<unsigned char>(clipf(r)), static_cast<unsigned char>(clipf(g)), 0};
}

inline Color vec2hsv(float vx, float vy) {
    if(vx*vx + vy*vy == 0)
        return {255, 255, 255};
    float angle = std::atan2(vy, vx);
    float hue = (angle > 0 ? angle : (2.f * PI + angle)) * 180.f / PI;
    float saturation = 100.0f * std::sqrt(vx * vx + vy * vy) / std::sqrt(2.0f);
    return hsv2rgb(hue, saturation, 100.0f);
}

int main(int argc, char const *argv[])
{
    // To time program execution
    using std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::duration;
    using std::chrono::milliseconds;
    auto t1 = high_resolution_clock::now();

    // Parse CLI and show help if needed
    CLIArgs args;
    try {
        args = parse_cli(argc, argv);
    } catch(const std::exception& e){
        const std::string msg{e.what()};

        if(msg == "help"){
            display_help();
            return 0;
        }

        std::cout << e.what()
        << "\n"
        << "Use option -h or --help to display usage" << std::endl;
        return 1;
    }

    // Possible paths
    std::string base = args.output_path.substr(0,args.output_path.length()-4);
    std::string input_luma_path = base + "_luma_ref.png";
    std::string target_luma_path = base + "_luma_target.png";
    std::string residue_path = base + "_residue.png";
    std::string prediction_path = base + "_prediction.png";

    // Log algorithm parameters
    if (args.verbose) {
        std::cout << "Reference:  " << args.input_path << "\n"
            << "Target: " << args.target_path << "\n"
            << "Output: " << args.output_path << "\n"
            << "\n"
            << "Running full block matching algorithm with config:\n"
            << "    --blocksize " << std::to_string(args.block_size) << "\n"
            << "    --maxsearch " << std::to_string(args.max_search) << "\n";
        if(args.export_luma){
            std::cout << "Reference Luma in BT.601 will be exported to "<< input_luma_path <<"\n"
                      << "Target Luma in BT.601 will be exported to "<< target_luma_path <<"\n";
        }
        if(args.export_residue){
            std::cout << "Residue image will be exported to " << residue_path << "\n";
        }
        std::cout << std::endl;
    }


    // Load input and target images
    int input_width, input_height, input_channels;
    unsigned char *input_data = stbi_load(args.input_path.c_str(), &input_width, &input_height, &input_channels, 0);
    if(!input_data){
        std::cout << "Failed to load reference image at " << args.input_path << std::endl;
        return 1;
    }

    int target_width, target_height, target_channels;
    unsigned char *target_data = stbi_load(args.target_path.c_str(), &target_width, &target_height, &target_channels, 0);
    if(!target_data){
        std::cout << "Failed to load target image at " << args.target_path << std::endl;
        return 1;
    }

    // Image size and channel validation
    if(input_width != target_width || input_height != target_height){
        std::cout << "Reference and target image do not have the same dimensions!" << std::endl;
        return 1;
    }
    if(input_channels != target_channels){
        std::cout << "Reference and target image do not have the same number of channels!" << std::endl;
        return 1;
    }

    int img_width = input_width;
    int img_height = input_height;
    int img_channels = input_channels;

    std::vector<unsigned char> input_y(img_width*img_height);
    std::vector<unsigned char> target_y(img_width*img_height);

    // Extract YUV 4:4:4 based on BT.601 and only keep the Y component
    if(args.verbose)
        std::cout << "Converting images to Y value (BT.601)..." << std::endl;
    #pragma omp parallel for collapse(2)
    for(int y=0; y < img_height; ++y){
        for(int x=0; x < img_width; ++x){
            int i = img_channels * (y * img_width + x);
            const unsigned char* ip = input_data + i;
            const unsigned char* tp = target_data + i;
            Color ic = srgb2bt601(ip[0], ip[1], ip[2]);
            Color tc = srgb2bt601(tp[0], tp[1], tp[2]);
            int i_y = y * img_width + x;
            input_y[i_y] = ic.y;
            target_y[i_y] = tc.y;
        }
    }

    // Free stbi data
    stbi_image_free(input_data);
    input_data= nullptr;
    stbi_image_free(target_data);
    target_data = nullptr;

    // Upscale if necessary
    if(args.subpixels > 0){
        target_width = target_width * (args.subpixels+1);
        target_height = target_height * (args.subpixels+1);
        target_y = upscaling_bilinear(target_y, img_width, img_height, args.subpixels+1);
    }

    // Export bt.601 luma values without converting to sRGB
    if(args.export_luma){
        if(args.verbose)
                std::cout << "Saving reference luma to " << input_luma_path << std::endl;
        int r = stbi_write_png(input_luma_path.c_str(), input_width, input_height, 1, input_y.data(), input_width);
        if(!r){
            std::cout << "Failed to write reference luma to " << input_luma_path << std::endl;
            return 1;
        }
        if(args.verbose)
                std::cout << "Saving upscaled target luma to " << target_luma_path << std::endl;
        r = stbi_write_png(target_luma_path.c_str(), target_width, target_height, 1, target_y.data(), target_width);
        if(!r){
            std::cout << "Failed to write upscaled target luma to " << target_luma_path << std::endl;
            return 1;
        }
    }

    // motion vectors
    int mv_width = img_width / args.block_size + img_width % args.block_size;
    int mv_height = img_height / args.block_size + img_height % args.block_size;

    std::vector<float> mv(mv_width*mv_height*2);

    if(args.verbose)
        std::cout << "Computing " << mv_width * mv_height << " motion vectors..." << std::endl;

    // Compute motion vector for each bloc
    int progress = 0;
    #pragma omp parallel for collapse(2)
    for (int by = 0; by < mv_height; ++by) {
        for (int bx = 0; bx < mv_width; ++bx) {
            fsbma(bx, by, args.block_size, args.max_search, mv, mv_width, input_y, target_y, input_width, input_height, target_width, target_height, args.subpixels+1);
            #pragma omp critical
            update_progress(&progress, mv_width * mv_height, args.verbose);
        }
    }

    // Convert motions vectors to optical flowmap using colors :
    //    Right -> red
    //    Left -> cyan
    //    Up -> violet
    //    down -> yellow
    // Each vector is normalized regarding the max allowed search

    if (args.verbose)
        std::cout << "Rendering optical flowmap (HSV colors)..." << std::endl;

    std::vector<unsigned char> flowmap(mv_width * mv_height * 3);
    #pragma omp parallel for collapse(2)
    for (int i=0; i < mv_height; ++i){
        for (int j = 0; j < mv_width; ++j) {
            float* v = &mv[0] + (2 * (i * mv_width + j));
            float vx = static_cast<float>(v[0]) / static_cast<float>(args.max_search);
            float vy = static_cast<float>(v[1]) / static_cast<float>(args.max_search);

            Color color = args.color_map == UV ? vec2uv(vx, vy) : vec2hsv(vx, vy);

            unsigned char* pixel = &flowmap[0] + (3 * (i * mv_width + j));
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
        }
    }

    int r = stbi_write_png(args.output_path.c_str(), mv_width, mv_height, 3, flowmap.data(), mv_width * 3);
    if(!r){
        std::cout << "Failed to write optical flowmap to " << args.output_path << std::endl;
    }


    if(args.export_residue || args.export_prediction){
        if(args.verbose)
                std::cout << "Computing prediction..." << std::endl;
        // Prediction with subpixel blending
        std::vector<float> prediction(img_width*img_height);
        std::vector<int> acc(img_width*img_height);
        #pragma omp parallel for collapse(2)
        for(int y=0; y < img_height; ++y){
            for(int x=0; x< img_width; ++x){
                

                // Get motion vector for this pixel
                int bi = (y / args.block_size) * mv_width + (x / args.block_size);
                float* v = &mv[0] + (2 * bi);

                // Move input pixel to this location
                int ivx = static_cast<int>(v[0]);
                int ivy = static_cast<int>(v[1]);

                float fx = std::abs(v[0]) - std::abs(ivx);
                float fy = std::abs(v[1]) - std::abs(ivy);

                int px = clamp(x + ivx, 0, img_width-1);
                int py = clamp(y + ivy, 0, img_height-1);

                float val = static_cast<float>(input_y[y * img_width + x]) / 255.f;

                // Integer motion => full value on predicted pixel
                if(v[0] == static_cast<float>(ivx) && v[1] == static_cast<float>(ivy)){
                    prediction[py * img_width + px] += val;
                    acc[py * img_width + px] += 1;
                } 
                else if(v[0] == 0 && v[1] > 0){
                    int oy = clamp(py + 1, 0, img_height-1);
                    prediction[py * img_width + px] += val * (1.f-fy);
                    acc[py * img_width + px] += 1;
                    prediction[oy * img_width + px] += val * fy;
                    acc[oy * img_width + px] += 1;
                } else if(v[0] == 0 && v[1] < 0){
                    int oy = clamp(py - 1, 0, img_height-1);
                    prediction[py * img_width + px] += val * (1.f-fy);
                    acc[py * img_width + px] += 1;
                    prediction[oy * img_width + px] += val * fy;
                    acc[oy * img_width + px] += 1;
                } else if (v[1] == 0 && v[0] > 0){
                    int ox = clamp(px + 1, 0, img_width-1);
                    prediction[py * img_width + px] += val * (1.f-fx);
                    acc[py * img_width + px] += 1;
                    prediction[py * img_width + ox] += val * fx;
                    acc[py * img_width + ox] += 1;
                } else if (v[1] == 0 && v[0] < 0){
                    int ox = clamp(px - 1, 0, img_width-1);
                    prediction[py * img_width + px] += val * (1.f-fx);
                    acc[py * img_width + px] += 1;
                    prediction[py * img_width + ox] += val * fx;
                    acc[py * img_width + ox] += 1;
                }else if (v[1] > 0 && v[0] > 0){
                    int ox = clamp(px + 1, 0, img_width-1);
                    int oy = clamp(py + 1, 0, img_height-1);

                    float top = val * (1.0f-fy);
                    float bottom = val * fy;
                    float top_left = top * (1.f-fx);
                    float top_right = top * fx;
                    float bottom_left = bottom * (1.f-fx);
                    float bottom_right = bottom * fx;

                    prediction[py * img_width + px] += top_left;
                    prediction[py * img_width + ox] += top_right;
                    prediction[oy * img_width + px] += bottom_left;
                    prediction[oy * img_width + ox] += bottom_right;

                    acc[py * img_width + px] += 1;
                    acc[py * img_width + ox] += 1;
                    acc[oy * img_width + px] += 1;
                    acc[oy * img_width + ox] += 1;
                } else if (v[1] < 0 && v[0] > 0){
                    int ox = clamp(px + 1, 0, img_width-1);
                    int oy = clamp(py - 1, 0, img_height-1);

                    float top = val * fy;
                    float bottom = val * (1.f-fy);
                    float top_left = top * (1.f-fx);
                    float top_right = top * fx;
                    float bottom_left = bottom * (1.f-fx);
                    float bottom_right = bottom * fx;

                    prediction[py * img_width + px] += bottom_left;
                    prediction[py * img_width + ox] += bottom_right;
                    prediction[oy * img_width + px] += top_left;
                    prediction[oy * img_width + ox] += top_right;
                    acc[py * img_width + px] += 1;
                    acc[py * img_width + ox] += 1;
                    acc[oy * img_width + px] += 1;
                    acc[oy * img_width + ox] += 1;
                } else if (v[1] > 0 && v[0] < 0){
                    int ox = clamp(px - 1, 0, img_width-1);
                    int oy = clamp(py + 1, 0, img_height-1);

                    float top = val * (1.0f-fy);
                    float bottom = val * fy;
                    float top_left = top * fx;
                    float top_right = top * (1.f-fx);
                    float bottom_left = bottom * fx;
                    float bottom_right = bottom * (1.f-fx);

                    prediction[py * img_width + px] += top_right;
                    prediction[py * img_width + ox] += top_left;
                    prediction[oy * img_width + px] += bottom_right;
                    prediction[oy * img_width + ox] += bottom_left;
                    acc[py * img_width + px] += 1;
                    acc[py * img_width + ox] += 1;
                    acc[oy * img_width + px] += 1;
                    acc[oy * img_width + ox] += 1;
                }else if (v[1] < 0 && v[0] < 0){
                    int ox = clamp(px - 1, 0, img_width-1);
                    int oy = clamp(py - 1, 0, img_height-1);

                    float top = val * fy;
                    float bottom = val * (1.f-fy);
                    float top_left = top * fx;
                    float top_right = top * (1.f-fx);
                    float bottom_left = bottom * fx;
                    float bottom_right = bottom * (1.f-fx);

                    prediction[py * img_width + px] += bottom_right;
                    prediction[py * img_width + ox] += bottom_left;
                    prediction[oy * img_width + px] += top_right;
                    prediction[oy * img_width + ox] += top_left;
                    acc[py * img_width + px] += 1;
                    acc[py * img_width + ox] += 1;
                    acc[oy * img_width + px] += 1;
                    acc[oy * img_width + ox] += 1;
                }
            }
        }
        // AVG blending + clamping + merge with input
        #pragma omp parallel for collapse(2)
        for(int y=0; y < img_height; ++y){
            for(int x=0; x< img_width; ++x){
                int n = acc[y * img_width + x];
                float val = prediction[y * img_width + x];
                if(n > 0)
                    val /= static_cast<float>(n);
                else
                    val = static_cast<float>(input_y[y * img_width + x]) / 255.f;
                prediction[y * img_width + x] = clampf(val, 0.f, 1.f);
            }
        }

        // Unormalizing values -> Predicted image
        std::vector<unsigned char> prediction_y(img_width*img_height);
        #pragma omp parallel for collapse(2)
        for(int y=0; y < img_height; ++y){
            for(int x=0; x< img_width; ++x){
                float val = prediction[y * img_width + x];
                prediction_y[y * img_width + x] = static_cast<unsigned char>(clampf(val * 255.f, 0, 255.f));
            }
        }

        if(args.export_prediction){
            if(args.verbose)
                std::cout << "Saving prediction to " << prediction_path << std::endl;
            int r = stbi_write_png(prediction_path.c_str(), img_width, img_height, 1, prediction_y.data(), img_width);
            if(!r){
                std::cout << "Failed to write prediction to " << prediction_path << std::endl;
                return 1;
            }
        }

        // Save residue
        if(args.export_residue){
            std::vector<unsigned char> residue(img_width*img_height);
            if(args.verbose)
                std::cout << "Computing Residue..." << std::endl;

            #pragma omp parallel for collapse(2)
            for(int y=0; y < img_height; ++y){
                for(int x=0; x< img_width; ++x){
                    int i = y * img_width + x;
                    residue[i] = prediction_y[i] - input_y[i];
                }
            }

            if(args.verbose)
                std::cout << "Saving Residue to " << residue_path << std::endl;
            int r = stbi_write_png(residue_path.c_str(), img_width, img_height, 1, residue.data(), img_width);
            if(!r){
                std::cout << "Failed to write residue to " << residue_path << std::endl;
                return 1;
            }
        }
    }


    if (args.verbose){
        auto t2 = high_resolution_clock::now();
        duration<double, std::milli> ms_double = t2 - t1;
        double seconds = ms_double.count() / 1000.0;

        std::cout << "Done. Took " << seconds << " s" << std::endl;
    }


    return 0;
}

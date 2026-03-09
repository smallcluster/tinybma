#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <omp.h>

#define PI 3.14159265358979311600f

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

#define DEFAULT_BLOCKSIZE 16
#define DEFAULT_MAXSEARCH 32
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

    return { static_cast<unsigned char>(clipf(r * 255.f + 0.5f)),
             static_cast<unsigned char>(clipf(g * 255.f + 0.5f)),
             static_cast<unsigned char>(clipf(b * 255.f + 0.5f)) };
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
    << "-s, --subpixels [INT]    How many allowed subpixels, set to 0 to disable subpixel motion. Default=" << DEFAULT_SUBPIXELS << "\n"
    << "-p, --prediction         Export luma prediction computed from the motion vectors.\n"
    << std::endl;
}

inline float interpolate_bilinear(float fx, float fy, float A, float B, float C, float D) {
    float w1 = (1 - fx) * (1 - fy);
    float w2 = fx * (1 - fy);
    float w3 = (1 - fx) * fy;
    float w4 = fx * fy;

    return A*w1 + B*w2 + C*w3 + D*w4;
}


inline unsigned int pixel_block_sad(int mx, int my, int bx, int by, int block_size, const std::vector<unsigned char>&input_y, const std::vector<unsigned char>&target_y, int img_width, int img_height){
    unsigned int sad = 0;
    for (int ky = 0; ky < block_size; ++ky) {
        for (int kx = 0; kx < block_size; ++kx) {
            int src_x = clamp(bx * block_size + kx, 0, img_width-1);
            int src_y = clamp(by * block_size + ky, 0, img_height-1);
            int dest_x = clamp(src_x + mx, 0, img_width-1);
            int dest_y = clamp(src_y + my, 0, img_height-1);
            int d = target_y[src_y * img_width + src_x] - input_y[dest_y * img_width + dest_x];
            sad += std::abs(d);
        }
    }
    return sad;
}

inline unsigned int subpixel_block_sad(int mx, int my, int sx, int sy, int subpixels, int bx, int by, int block_size, const std::vector<unsigned char>& input_y, const std::vector<unsigned char>& target_y, int img_width, int img_height) {
    unsigned int sad = 0;
    for (int ky = 0; ky < block_size; ++ky) {
        for (int kx = 0; kx < block_size; ++kx) {
            int src_x = clamp(bx * block_size + kx, 0, img_width - 1);
            int src_y = clamp(by * block_size + ky, 0, img_height - 1);

            float mvx = static_cast<float>(mx) + static_cast<float>(sx) / static_cast<float>(subpixels + 1);
            float mvy = static_cast<float>(my) + static_cast<float>(sy) / static_cast<float>(subpixels + 1);

            float ref_x = src_x + mvx;
            float ref_y = src_y + mvy;

            int ix = std::floor(ref_x);
            int iy = std::floor(ref_y);
            float fx = ref_x - ix;
            float fy = ref_y - iy;

            ix = clamp(ix, 0, img_width - 2);
            iy = clamp(ix, 0, img_height - 2);

            unsigned char A = input_y[iy * img_width + ix];
            unsigned char B = input_y[iy * img_width + ix + 1];
            unsigned char C = input_y[(iy + 1) * img_width + ix];
            unsigned char D = input_y[(iy + 1) * img_width + ix + 1];

            float valf = interpolate_bilinear(fx, fy, A, B, C, D);
            unsigned char val = static_cast<unsigned char>(clampf(valf + 0.5f, 0.f, 255.f));
            int d = target_y[src_y * img_width + src_x] - val;
            sad += std::abs(d);
        }
    }
    return sad;
}

inline void fsbma(int bx, int by, int block_size, int max_search, std::vector<float>&mv, int mv_width, const std::vector<unsigned char>&input_y, const std::vector<unsigned char>&target_y, int img_width, int img_height, int subpixels){
    // Use current position as the initial best MSE
    // Ensures the best selected vector is (0,0) if all motions are equal.
    int best_mv_x = 0;
    int best_mv_y = 0;
    unsigned int best_sad = pixel_block_sad(best_mv_x, best_mv_y, bx, by, block_size, input_y, target_y, img_width, img_height);
    // Perform lookup over all possible integer direction and keep the best one
    for (int my = -max_search; my <= max_search; ++my) {
        for (int mx = -max_search; mx <= max_search; ++mx) {
            // The no motion case is already precomputed
            if(my == 0 && mx == 0)
                continue;
            unsigned int  sad = pixel_block_sad(mx, my, bx, by, block_size, input_y, target_y, img_width, img_height);
            if (sad < best_sad) {
                best_sad = sad;
                best_mv_x = mx;
                best_mv_y = my;
            }
        }
    }
    // Perform lookup over all possible subpixels direction and keep the best one
    int best_mv_sx = 0;
    int best_mv_sy = 0;
    for (int my = -subpixels; my <= subpixels; ++my) {
        for (int mx = -subpixels; mx <= subpixels; ++mx) {
            // The no motion case is already precomputed
            if (my == 0 && mx == 0)
                continue;
            unsigned int  sad = subpixel_block_sad(best_mv_x, best_mv_y, mx, my, subpixels, bx, by, block_size, input_y, target_y, img_width, img_height);
            if (sad < best_sad) {
                best_sad = sad;
                best_mv_sx = mx;
                best_mv_sy = my;
            }
        }
    }

    // Store best motion vector
    float* v = &mv[0] + (2 * (by * mv_width + bx));
    v[0] = static_cast<float>(best_mv_x) + static_cast<float>(best_mv_sx) / static_cast<float>(subpixels + 1);
    v[1] = static_cast<float>(best_mv_y) + static_cast<float>(best_mv_sy) / static_cast<float>(subpixels + 1);
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
    return {static_cast<unsigned char>(clipf(r + 0.5f)), static_cast<unsigned char>(clipf(g + 0.5f)), 0};
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
            << "    --maxsearch " << std::to_string(args.max_search) << "\n"
            << "    --subpixels " << std::to_string(args.subpixels)  << "\n"
            << "    --colormap " << (args.color_map == HSV ? "hsv" : "uv")  << "\n";
        if(args.export_luma){
            std::cout << "Reference Luma in BT.601 will be exported to "<< input_luma_path <<"\n"
                      << "Target Luma in BT.601 will be exported to "<< target_luma_path <<"\n";
        }
        if(args.export_residue){
            std::cout << "Residue image will be exported to " << residue_path << "\n";
        }
        if(args.export_prediction){
            std::cout << "Predicted image will be exported to " << prediction_path << "\n";
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

    std::vector<unsigned char> input_y(input_width * input_height);
    std::vector<unsigned char> target_y(input_width * input_height);

    // Extract YUV 4:4:4 based on BT.601 and only keep the Y component
    if(args.verbose)
        std::cout << "Converting images to Y value (BT.601)..." << std::endl;
    #pragma omp parallel for collapse(2)
    for(int y=0; y < input_height; ++y){
        for(int x=0; x < input_width; ++x){
            int i = input_channels * (y * input_width + x);
            const unsigned char* ip = input_data + i;
            const unsigned char* tp = target_data + i;
            Color ic = srgb2bt601(ip[0], ip[1], ip[2]);
            Color tc = srgb2bt601(tp[0], tp[1], tp[2]);
            int i_y = y * input_width + x;
            input_y[i_y] = ic.y;
            target_y[i_y] = tc.y;
        }
    }

    // Free stbi data
    stbi_image_free(input_data);
    input_data= nullptr;
    stbi_image_free(target_data);
    target_data = nullptr;

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
        r = stbi_write_png(target_luma_path.c_str(), input_width, input_height, 1, target_y.data(), input_width);
        if(!r){
            std::cout << "Failed to write upscaled target luma to " << target_luma_path << std::endl;
            return 1;
        }
    }

    // motion vectors
    int mv_width = input_width / args.block_size + input_width % args.block_size;
    int mv_height = input_height / args.block_size + input_height % args.block_size;

    std::vector<float> mv(mv_width*mv_height*2);

    if(args.verbose)
        std::cout << "Computing " << mv_width * mv_height << " motion vectors..." << std::endl;

    // Compute motion vector for each bloc
    int progress = 0;
    #pragma omp parallel for collapse(2)
    for (int by = 0; by < mv_height; ++by) {
        for (int bx = 0; bx < mv_width; ++bx) {
            fsbma(bx, by, args.block_size, args.max_search, mv, mv_width, input_y, target_y, input_width, input_height, args.subpixels);
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
            float md = static_cast<float>(args.subpixels) / static_cast<float>(args.subpixels + 1) + static_cast<float>(args.max_search);
            float vx = v[0] / md;
            float vy = v[1] / md;
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

        // Prediction with subpixel
        std::vector<unsigned char> prediction_y(input_width*input_height);

        #pragma omp parallel for collapse(2)
        for (int y = 0; y < input_height; ++y) {
            for (int x = 0; x < input_width; ++x) {
                float* v = &mv[0] + (2 * ( (y / args.block_size) * mv_width + (x / args.block_size)));

                float ref_x = x + v[0];
                float ref_y = y + v[1];

                int ix = std::floor(ref_x);
                int iy = std::floor(ref_y);
                float fx = ref_x - ix;
                float fy = ref_y - iy;

                float lower_motion = 1.f / static_cast<float>(2*args.subpixels+1);

                // Copy pixel value from input
                if (std::abs(fx) < lower_motion  && std::abs(fy) < lower_motion) {
                    ix = clamp(ix, 0, input_width - 1);
                    iy = clamp(iy, 0, input_height - 1);
                    prediction_y[y * input_width + x] = input_y[iy * input_width + ix];
                }
                else {
                    // Copy interpolated value
                    ix = clamp(ix, 0, input_width - 2);
                    iy = clamp(iy, 0, input_height - 2);

                    unsigned char A = input_y[iy * input_width + ix];
                    unsigned char B = input_y[iy * input_width + ix + 1];
                    unsigned char C = input_y[(iy + 1) * input_width + ix];
                    unsigned char D = input_y[(iy + 1) * input_width + ix + 1];

                    float valf = interpolate_bilinear(fx, fy, A, B, C, D);

                    unsigned char val = static_cast<unsigned char>(clampf(valf + 0.5f, 0.f, 255.f));
                    prediction_y[y * input_width + x] = val;
                }

            }
        }
        

        if(args.export_prediction){
            if(args.verbose)
                std::cout << "Saving prediction to " << prediction_path << std::endl;
            int r = stbi_write_png(prediction_path.c_str(), input_width, input_height, 1, prediction_y.data(), input_width);
            if(!r){
                std::cout << "Failed to write prediction to " << prediction_path << std::endl;
                return 1;
            }
        }

        // Save residue
        if(args.export_residue){
            std::vector<unsigned char> residue(input_width* input_height);
            if(args.verbose)
                std::cout << "Computing Residue..." << std::endl;

            #pragma omp parallel for collapse(2)
            for(int y=0; y < input_height; ++y){
                for(int x=0; x< input_width; ++x){
                    int i = y * input_width + x;
                    float val = clampf(0.5f * (255.f + static_cast<float>(prediction_y[i] - target_y[i])) + 0.5f, 0, 255);
                    residue[i] = static_cast<unsigned char>(val);
                }
            }

            if(args.verbose)
                std::cout << "Saving Residue to " << residue_path << std::endl;
            int r = stbi_write_png(residue_path.c_str(), input_width, input_height, 1, residue.data(), input_width);
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

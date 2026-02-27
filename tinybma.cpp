#include <iostream>
#include <string>
#include <vector>
#include <omp.h>

#define PI 3.14159265358979311600f

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

#define DEFAULT_BLOCKSIZE 16
#define DEFAULT_MAXSEARCH 48
#define INPUT_ARG_STR "INPUT_IMG"
#define TARGET_ARG_STR "TARGET_IMG"
#define OUTPUT_ARG_STR "OUTPUT_IMG"
#define FULL_BLOCKSIZE_ARG_STR "blocksize"
#define FULL_MAXSEARCH_ARG_STR "maxsearch"
#define FULL_VERBOSE_ARG_STR "verbose"
#define BLOCKSIZE_ARG_STR "b"
#define MAXSEARCH_ARG_STR "m"
#define VERBOSE_ARG_STR "v"


//------------------------------------------------------------------------------------------------------------------------------
//  HSV TO RGB
//------------------------------------------------------------------------------------------------------------------------------
struct RGBColor {
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

RGBColor hsv2rgb(float H, float S, float V) {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;

    float h = H / 360.f;
    float s = S / 100.f;
    float v = V / 100.f;
    
    int i = std::floor(h * 6.f);
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
    
    return { static_cast<unsigned char>(r * 255.f), 
             static_cast<unsigned char>(g * 255.f), 
             static_cast<unsigned char>(b * 255.f) };
}

//------------------------------------------------------------------------------------------------------------------------------
//  CLI PARSING
//------------------------------------------------------------------------------------------------------------------------------
struct CLIArgs {
    int block_size = DEFAULT_BLOCKSIZE;
    int max_search = DEFAULT_MAXSEARCH;
    std::string input_path;
    std::string target_path;
    std::string output_path;
    bool verbose = false;
};

bool check_arg(const std::string& a, const std::string& full_name, const std::string& short_name){
    return a == "-"+short_name || a == "--"+full_name;
}

int parse_block_size(const std::string& v){
    const std::string error_msg = "--"+std::string{FULL_BLOCKSIZE_ARG_STR}+" (-"+std::string{BLOCKSIZE_ARG_STR}+")"+" must be a striclty positive number";
    int num;
    try{
        num = std::stoi(v);
    } catch(const std::exception& e){
        throw std::runtime_error(error_msg);
    }
    if(num <= 0)
        throw std::runtime_error(error_msg);
    return num;
}

int parse_max_search(const std::string& v){
    const std::string error_msg = "--"+std::string{FULL_MAXSEARCH_ARG_STR}+" (-"+std::string{MAXSEARCH_ARG_STR}+")"+" must be a striclty positive number";
    int num;
    try{
        num = std::stoi(v);
    } catch(const std::exception& e){
        throw std::runtime_error(error_msg);
    }
    if(num <= 0)
        throw std::runtime_error(error_msg);
    return num;
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


            if (check_arg(a, FULL_VERBOSE_ARG_STR, VERBOSE_ARG_STR)) {
                args.verbose = true;
                continue;
            }
                

            if(i+1 >= argc)
                throw std::runtime_error("Missing value for option "+a);
            ++i;
            std::string v{argv[i]};
            if(check_arg(a, FULL_BLOCKSIZE_ARG_STR, BLOCKSIZE_ARG_STR)){
                try {
                    args.block_size = parse_block_size(v);
                } catch(const std::exception& e){
                    throw e;
                }
            } else if(check_arg(a, FULL_MAXSEARCH_ARG_STR, MAXSEARCH_ARG_STR)){
                try {
                    args.max_search = parse_max_search(v);
                } catch(const std::exception& e){
                    throw e;
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
        throw std::runtime_error("Missing required positional argument "+std::string{INPUT_ARG_STR});
    if(args.target_path == "")
        throw std::runtime_error("Missing required positional argument "+std::string{TARGET_ARG_STR});
    if(args.output_path == "")
        throw std::runtime_error("Missing required positional argument "+std::string{OUTPUT_ARG_STR});


    return std::move(args);
}

//------------------------------------------------------------------------------------------------------------------------------
//  MAIN PROGRAM
//------------------------------------------------------------------------------------------------------------------------------

void display_help(){
    std::cout << "USAGE: tinybma [OPTION]... " << INPUT_ARG_STR << " " << TARGET_ARG_STR << " " <<  OUTPUT_ARG_STR << "\n" 
    << "\n"
    << "Generate an optical flowmap from " << INPUT_ARG_STR <<" to " << TARGET_ARG_STR << " using a full search block matching algorithm.\n" 
    << "\n"
    << "EXAMPLE: tinybma -b 16 -m 32 ref_start.png ref_end.png flowmap.png\n"
    << "\n"
    << "OPTIONS:\n" 
    << "-h, --help           Display this help. (optional)\n"
    << "-"<< BLOCKSIZE_ARG_STR << ", --" << FULL_BLOCKSIZE_ARG_STR <<"      Block size. Default=" << DEFAULT_BLOCKSIZE << "\n" 
    << "-"<< MAXSEARCH_ARG_STR << ", --" << FULL_MAXSEARCH_ARG_STR <<"      Maximum allowed block movement in whole pixels. Default=" << DEFAULT_MAXSEARCH <<"\n" 
    << "-"<< VERBOSE_ARG_STR   << ", --" << FULL_VERBOSE_ARG_STR   <<"      Display infos and progression. (optional)\n"
    << ""<< std::endl;
}

int main(int argc, char const *argv[])
{

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

    // Log algorithm parameters
    if (args.verbose) {
        std::cout << "Input:  " << args.input_path << "\n"
            << "Target: " << args.target_path << "\n"
            << "Output: " << args.output_path << "\n"
            << "\n"
            << "Running full block matching algorithm with config:\n"
            << "    --blocksize " << std::to_string(args.block_size) << "\n"
            << "    --maxsearch " << std::to_string(args.max_search) << "\n"

            << std::endl;
    }
    

    // Load input and target images
    int input_width, input_height, input_channels;
    unsigned char *input_data = stbi_load(args.input_path.c_str(), &input_width, &input_height, &input_channels, 0);
    if(!input_data){
        std::cout << "Impossible to load input image!" <<std::endl;
        return 1;
    }

    int target_width, target_height, target_channels;
    unsigned char *target_data = stbi_load(args.target_path.c_str(), &target_width, &target_height, &target_channels, 0);
    if(!target_data){
        std::cout << "Impossible to load target image!" <<std::endl;
        return 1;
    }

    // Image size and channel validation
    if(input_width != target_width || input_height != target_height){
        std::cout << "Input and target image need to be of the same dimensions!" << std::endl;
        return 1;
    }
    if(input_channels != target_channels){
        std::cout << "Input and target image need to have the same number of channels!" << std::endl;
        return 1;
    }

    // motion vectors
    int mv_width = input_width / args.block_size + input_width % args.block_size;
    int mv_height = input_height / args.block_size + input_height % args.block_size;

    std::vector<int> mv(mv_width*mv_height*2);

    if(args.verbose)
        std::cout << "Computing " << mv_width * mv_height << " motion vectors..." << std::endl;

    // Compute motion vector for each bloc
    int progress = 0;
    #pragma omp parallel for collapse(2)
    for (int by = 0; by < mv_height; ++by) {
        for (int bx = 0; bx < mv_width; ++bx) {
            // Perform lookup in target image over all possible direction
            // and keep the best one
            int best_mv_x = 0;
            int best_mv_y = 0;
            float best_mse = 1e7;
            //int best_sad = INT_MAX;
            int used = 0;
            for (int my = -args.max_search; my <= args.max_search; ++my) {
                for (int mx = -args.max_search; mx <= args.max_search; ++mx) {
                    // Compute SAD
                    //int sad = 0;
                    float mse = 0;
                    for (int ky = 0; ky < args.block_size; ++ky) {
                        for (int kx = 0; kx < args.block_size; ++kx) {

                            int src_x = bx * args.block_size + kx;
                            int src_y = by * args.block_size + ky;
                            int dest_x = src_x + mx;
                            int dest_y = src_y + my;

                            // Ignore out of bounds pixels
                            if (src_x >= input_width || src_y >= input_height 
                                || dest_x < 0 || dest_y < 0 
                                || dest_x >= target_width || dest_y >= target_height)
                                continue;
                            
                            ++used;

                            const unsigned char* src_pixel = input_data + (input_channels * (src_y * input_width + src_x));
                            const unsigned char* dest_pixel = target_data + (target_channels * (dest_y * target_width + dest_x));

                            for (int c = 0; c < input_channels; ++c) {
                                int d = src_pixel[c] - dest_pixel[c];
                                mse += static_cast<float>(d*d);
                            }
                                
                                //sad += std::abs(src_pixel[c] - dest_pixel[c]);
                        }
                    }
                    mse /= static_cast<float>(used);
                    if (mse < best_mse) {
                        best_mse = mse;
                        best_mv_x = mx;
                        best_mv_y = my;
                    }
                }
            }
            // Store best
            int* v = &mv[0] + (2 * (by * mv_width + bx));
            v[0] = best_mv_x;
            v[1] = best_mv_y;

            // Log progress
            #pragma omp critical
            {
                if (args.verbose) {
                    int previous = static_cast<int>(100.0f * static_cast<float>(progress) / static_cast<float>(mv_width * mv_height));
                    ++progress;
                    int next = static_cast<int>(100.0f * static_cast<float>(progress) / static_cast<float>(mv_width * mv_height));
                    if (previous != next)
                        std::cout << "Progress: " << progress << "/" << mv_width * mv_height << " (" << next << "%)" << std::endl;
                }
            }

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
    for (int i=0; i < mv_height; ++i){
        for (int j = 0; j < mv_width; ++j) {

            int* v = &mv[0] + (2 * (i * mv_width + j));
            float vx = static_cast<float>(v[0]);
            float vy = static_cast<float>(v[1]);

            float angle = std::atan2(vy, vx);

            float hue = (angle > 0 ? angle : (2.f * PI + angle)) * 180.f / PI;

            float max_length = std::sqrt(static_cast<float>(2 * args.max_search * args.max_search));
            float saturation = 100.0f * std::sqrt(vx * vx + vy * vy) / max_length;

            RGBColor color = hsv2rgb(hue, saturation, 100.0f);

            unsigned char* pixel = &flowmap[0] + (3 * (i * mv_width + j));
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
        }
    }

    stbi_write_png(args.output_path.c_str(), mv_width, mv_height, 3, flowmap.data(), mv_width * 3);

    if (args.verbose)
        std::cout << "Done." << std::endl;

    return 0;
}

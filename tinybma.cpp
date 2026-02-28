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

Color hsv2rgb(float H, float S, float V) {
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
    
    return { static_cast<unsigned char>(r * 255.f), 
             static_cast<unsigned char>(g * 255.f), 
             static_cast<unsigned char>(b * 255.f) };
}

// Adapted from https://learn.microsoft.com/fr-fr/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering#converting-rgb888-to-yuv-444
Color srgb2bt601(unsigned char r, unsigned char g, unsigned char b) {
    unsigned char y = ( (  66 * r + 129 * g +  25 * b + 128) >> 8) +  16;
    unsigned char u = ( ( -38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
    unsigned char v = ( ( 112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
    return {y, u, v};
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
    std::string input_path;
    std::string target_path;
    std::string output_path;
    bool export_luma = false;
    bool export_residue = false;
    bool verbose = false;
    ColorMaps color_map = HSV;
};

bool check_arg(const std::string& a, const std::string& full_name, const std::string& short_name){
    return a == "-"+short_name || a == "--"+full_name;
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
            }else if(check_arg(a, "colormap", "c")){
                try{
                    args.color_map = parse_color_map(v);
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
    << "-l, --luma               Export converted sRGB -> YUV 4:4:4 BT.601 's luma component of the REF_IMG and TARGET_IMG."
    << "-r, --residue            Export the luma residue to reconstruct the TARGET_IMG's luma value.\n"
    << "-c, --colormap [hsv|uv]  Choose the colormap to visualize the optical flow. Default=hsv\n"
    << std::endl;
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

    // Possible paths
    std::string base = args.output_path.substr(0,args.output_path.length()-4);  
    std::string input_luma_path = base + "_luma_ref.png";
    std::string target_luma_path = base + "_luma_target.png";
    std::string residue_path = base + "_residue.png";

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

    // Export luma values
    if(args.export_luma){
        int r = stbi_write_png(input_luma_path.c_str(), input_width, input_height, 1, input_y.data(), input_width);
        if(!r){
            std::cout << "Failed to write reference luma to " << input_luma_path << std::endl;
            return 1;
        }
        r = stbi_write_png(target_luma_path.c_str(), input_width, input_height, 1, target_y.data(), input_width);
        if(!r){
            std::cout << "Failed to write target luma to " << target_luma_path << std::endl;
            return 1;
        }
    }


    // motion vectors
    int mv_width = img_width / args.block_size + img_width % args.block_size;
    int mv_height = img_height / args.block_size + img_height % args.block_size;

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

                            // Ignore out of bounds pixels from the current bloc
                            if (src_x >= img_width || src_y >= img_height)
                                continue;
                            
                            ++used;
                            
                            // Consider out of bound pixel after moving to be full of zeros
                            int s;
                            if(dest_x < 0 || dest_y < 0 || dest_x >= img_width || dest_y >= img_height)
                                s = 0;
                            else 
                                s = input_y[dest_y * img_width + dest_x];

                            int d = target_y[src_y * img_width + src_x] - s;
                        
                            mse += static_cast<float>(d*d);
                        }
                    }
                    mse /= static_cast<float>(used*used);
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

    #pragma omp parallel for collapse(2)
    for (int i=0; i < mv_height; ++i){
        for (int j = 0; j < mv_width; ++j) {

            int* v = &mv[0] + (2 * (i * mv_width + j));
            float vx = static_cast<float>(v[0]);
            float vy = static_cast<float>(v[1]);

            Color color;
            if(args.color_map == UV){
                float r =  0.5f * 255.0f * (1.0f + static_cast<float>(vx) / static_cast<float>(args.max_search));
                r = r < 0 ? 0 : r > 255 ? 255 : r;
                float g = 0.5f * 255.0f * (1.0f + static_cast<float>(vy) / static_cast<float>(args.max_search));
                g = g < 0 ? 0 : g > 255 ? 255 : g;
                color.r = static_cast<unsigned char>(r);
                color.g = static_cast<unsigned char>(g);
                color.b = 0;
            } else if(args.color_map == HSV){
                if(vx*vx + vy*vy > 0){
                    float angle = std::atan2(vy, vx);
                    float hue = (angle > 0 ? angle : (2.f * PI + angle)) * 180.f / PI;
                    float max_length = std::sqrt(static_cast<float>(2 * args.max_search * args.max_search));
                    float saturation = 100.0f * std::sqrt(vx * vx + vy * vy) / max_length;
                    color = hsv2rgb(hue, saturation, 100.0f);
                } else
                    color = {255, 255, 255};
            }
            
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

    if(args.export_residue){
        std::vector<unsigned char> residue(img_width*img_height);
        if(args.verbose)
            std::cout << "Computing Residue..." << std::endl;

        #pragma omp parallel for collapse(2)
        for(int y=0; y < img_height; ++y){
            for(int x=0; x< img_width; ++x){
                // Get motion vector for this pixel
                int bi = (y / args.block_size) * mv_width + (x / args.block_size);
                int* v = &mv[0] + (2 * bi);

                // Clamped predicition coords
                int px = x + v[0];
                int py = y + v[1];
                if(px < 0)
                    px = 0;
                if(px >= img_width)
                    px = img_width-1;
                if(py < 0)
                    py = 0;
                if(py >= img_height)
                    py = img_height-1;

                int i = y * img_width + x;
                int pi = py * img_width + px;

                // Residue values
                residue[i] = target_y[i] - input_y[pi];
            }
        }

        int r = stbi_write_png(residue_path.c_str(), img_width, img_height, 1, residue.data(), img_width);
        if(!r){
            std::cout << "Failed to write residue to " << residue_path << std::endl;
            return 1;
        }
    }

    if (args.verbose)
        std::cout << "Done." << std::endl;

    return 0;
}

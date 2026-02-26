#include <iostream>
#include <string>

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
#define BLOCKSIZE_ARG_STR "b"
#define MAXSEARCH_ARG_STR "m"


//------------------------------------------------------------------------------------------------------------------------------
//  CLI PARSING
//------------------------------------------------------------------------------------------------------------------------------
struct CLIArgs {
    int block_size = DEFAULT_BLOCKSIZE;
    int max_search = DEFAULT_MAXSEARCH;
    std::string input_path;
    std::string target_path;
    std::string output_path;
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
    const std::string error_msg = "--"+std::string{FULL_MAXSEARCH_ARG_STR}+" (-"+std::string{MAXSEARCH_ARG_STR}+")"+" must be a positive number";
    int num;
    try{
        num = std::stoi(v);
    } catch(const std::exception& e){
        throw std::runtime_error(error_msg);
    }
    if(num < 0)
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
    << "-h, --help           Display this help.\n"
    << "-"<< BLOCKSIZE_ARG_STR << ", --" << FULL_BLOCKSIZE_ARG_STR <<"      Block size. Default=" << DEFAULT_BLOCKSIZE << "\n" 
    << "-"<< MAXSEARCH_ARG_STR << ", --" << FULL_MAXSEARCH_ARG_STR <<"      Maximum allowed block movement in whole pixels. Default=" << DEFAULT_MAXSEARCH <<"\n" 
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
    std::cout << "Input:  "+args.input_path+"\n"
              << "Target: "+args.target_path+"\n"
              << "Output: "+args.output_path+"\n"
              << "\n"
              << "Running full block matching algorithm with config:\n"
              << "    --blocksize "+std::to_string(args.block_size)+"\n"
              << "    --maxsearch "+std::to_string(args.max_search)+"\n"
              
              << std::endl;

    // TODO: perform FBM on whole displacements

    return 0;
}

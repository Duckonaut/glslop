#include <glslang/Include/glslang_c_interface.h>

// Required for use of glslang_default_resource
#include <glslang/Public/resource_limits_c.h>

#include <optional>
#include <string_view>
#include <string>
#include <fstream>

#include <stdio.h>
#include <stdint.h>

struct SpirVBinary {
    uint32_t* words; // SPIR-V words
    int size;        // number of words in SPIR-V binary
};

SpirVBinary compileShaderToSPIRV_Vulkan(
    glslang_stage_t stage,
    const char* shaderSource,
    const char* fileName
) {
    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_2,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_5,
        .code = shaderSource,
        .default_version = 100,
        .default_profile = GLSLANG_NO_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .resource = glslang_default_resource(),
        .callbacks = NULL,
        .callbacks_ctx = NULL,
    };

    glslang_shader_t* shader = glslang_shader_create(&input);

    SpirVBinary bin = {
        .words = NULL,
        .size = 0,
    };
    if (!glslang_shader_preprocess(shader, &input)) {
        printf("GLSL preprocessing failed %s\n", fileName);
        printf("%s\n", glslang_shader_get_info_log(shader));
        printf("%s\n", glslang_shader_get_info_debug_log(shader));
        printf("%s\n", input.code);
        glslang_shader_delete(shader);
        return bin;
    }

    if (!glslang_shader_parse(shader, &input)) {
        printf("GLSL parsing failed %s\n", fileName);
        printf("%s\n", glslang_shader_get_info_log(shader));
        printf("%s\n", glslang_shader_get_info_debug_log(shader));
        printf("%s\n", glslang_shader_get_preprocessed_code(shader));
        glslang_shader_delete(shader);
        return bin;
    }

    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(
            program,
            GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT
        )) {
        printf("GLSL linking failed %s\n", fileName);
        printf("%s\n", glslang_program_get_info_log(program));
        printf("%s\n", glslang_program_get_info_debug_log(program));
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return bin;
    }

    glslang_program_SPIRV_generate(program, stage);

    bin.size = glslang_program_SPIRV_get_size(program);
    bin.words = (uint32_t*)malloc(bin.size * sizeof(uint32_t));
    glslang_program_SPIRV_get(program, bin.words);

    const char* spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages)
        printf("(%s) %s\b", fileName, spirv_messages);

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    return bin;
}

struct Args {
    std::string inputFile;
    std::string outputFile;
    glslang_stage_t stage;

    static glslang_stage_t guessStageFromFileName(const std::string& fileName) {
        if (fileName.find(".vert") != std::string::npos) {
            return GLSLANG_STAGE_VERTEX;
        } else if (fileName.find(".frag") != std::string::npos) {
            return GLSLANG_STAGE_FRAGMENT;
        } else if (fileName.find(".comp") != std::string::npos) {
            return GLSLANG_STAGE_COMPUTE;
        } else {
            return GLSLANG_STAGE_VERTEX;
        }
    }

    Args(int argc, char* argv[]) {
        std::optional<std::string> inputFile;
        std::optional<std::string> outputFile;
        std::optional<glslang_stage_t> stage;

        for (int i = 1; i < argc; i++) {
            std::string_view arg = argv[i];

            if (arg == "-o") {
                if (i + 1 < argc) {
                    outputFile = argv[++i];
                } else {
                    printf("No output file specified\n");
                    exit(1);
                }
            } else if (arg == "-s") {
                if (i + 1 < argc) {
                    std::string_view stageStr = argv[++i];
                    if (stageStr == "vert" || stageStr == "vertex") {
                        stage = GLSLANG_STAGE_VERTEX;
                    } else if (stageStr == "frag" || stageStr == "fragment") {
                        stage = GLSLANG_STAGE_FRAGMENT;
                    } else if (stageStr == "comp" || stageStr == "compute") {
                        stage = GLSLANG_STAGE_COMPUTE;
                    } else {
                        printf("Unknown stage %s\n", stageStr.data());
                        exit(1);
                    }
                } else {
                    printf("No stage specified\n");
                    exit(1);
                }
            } else {
                inputFile = arg;
            }
        }

        if (inputFile) {
            this->inputFile = *inputFile;
            if (!outputFile) {
                this->outputFile = this->inputFile + ".spv";
            } else {
                this->outputFile = *outputFile;
            }
            if (stage) {
                this->stage = *stage;
            } else {
                this->stage = guessStageFromFileName(this->inputFile);
            }
        } else {
            printf("No input file specified\n");
            exit(1);
        }
    }
};

int main(int argc, char* argv[]) {
    Args args(argc, argv);

    glslang_initialize_process();

    std::ifstream file(args.inputFile);
    if (!file.is_open()) {
        printf("Failed to open file %s\n", args.inputFile.c_str());
        return 1;
    }

    std::string shaderSource(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    SpirVBinary bin = compileShaderToSPIRV_Vulkan(
        args.stage,
        shaderSource.c_str(),
        args.inputFile.c_str()
    );

    if (bin.words) {
        std::ofstream outFile(args.outputFile, std::ios::binary);
        if (!outFile.is_open()) {
            printf("Failed to open file %s\n", args.outputFile.c_str());
            return 1;
        }

        outFile.write((char*)bin.words, bin.size * sizeof(uint32_t));
        outFile.close();

        free(bin.words);
    }

    glslang_finalize_process();

    return 0;
}

#include <glslang/Include/Common.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <glslang/Include/intermediate.h>

#include <optional>
#include <sstream>
#include <string_view>
#include <string>
#include <fstream>

#include <stdio.h>
#include <stdint.h>

struct Args {
    std::string inputFile;
    std::string outputFile;
    std::optional<std::string> structPrefix;
    std::optional<std::string> globalPrefix;
    EShLanguage stage;

    static EShLanguage guessStageFromFileName(const std::string& fileName) {
        if (fileName.find(".vert") != std::string::npos) {
            return EShLanguage::EShLangVertex;
        } else if (fileName.find(".frag") != std::string::npos) {
            return EShLanguage::EShLangFragment;
        } else if (fileName.find(".comp") != std::string::npos) {
            return EShLanguage::EShLangCompute;
        } else {
            return EShLanguage::EShLangVertex; // default to vertex shader
        }
    }

    Args(int argc, char* argv[]) {
        std::optional<std::string> inputFile;
        std::optional<std::string> outputFile;
        std::optional<EShLanguage> stage;

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
                        stage = EShLanguage::EShLangVertex;
                    } else if (stageStr == "frag" || stageStr == "fragment") {
                        stage = EShLanguage::EShLangFragment;
                    } else if (stageStr == "comp" || stageStr == "compute") {
                        stage = EShLanguage::EShLangCompute;
                    } else {
                        printf("Unknown stage %s\n", stageStr.data());
                        exit(1);
                    }
                } else {
                    printf("No stage specified\n");
                    exit(1);
                }
            } else if (arg == "-p") {
                if (i + 1 < argc) {
                    structPrefix = argv[++i];
                } else {
                    printf("No struct prefix specified\n");
                    exit(1);
                }
            } else if (arg == "-g") {
                if (i + 1 < argc) {
                    globalPrefix = argv[++i];
                } else {
                    printf("No global prefix specified\n");
                    exit(1);
                }
            } else {
                inputFile = arg;
            }
        }

        if (inputFile) {
            this->inputFile = *inputFile;
            if (!outputFile) {
                size_t lastSlash = this->inputFile.find_last_of("/\\");
                size_t lastDot = this->inputFile.find_last_of(".");
                if (lastDot == std::string::npos) {
                    lastDot = this->inputFile.size();
                }

                if (lastSlash == std::string::npos) {
                    lastSlash = 0;
                } else {
                    lastSlash++;
                }

                size_t start = lastSlash;
                size_t end = lastDot;

                this->outputFile = this->inputFile.substr(start, end - start) + ".h";
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

class BasicIncluder : public glslang::TShader::Includer {
  public:
    IncludeResult*
    includeLocal(const char* headerName, const char* includerName, size_t) override {
        std::string headerPath = std::string(includerName) + "/" + headerName;

        std::ifstream file(headerPath);
        if (!file.is_open()) {
            printf("Failed to open include file %s\n", headerPath.c_str());
            return nullptr;
        }

        std::string headerSource(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        return new IncludeResult(
            headerName,
            headerSource.c_str(),
            headerSource.size(),
            nullptr
        );
    }

    void releaseInclude(IncludeResult* result) override {
        delete result;
    }
};

static glslang::TProgram*
CompileShader(const char* shaderSource, const char* fileName, EShLanguage stage) {
    glslang::TShader* shader = new glslang::TShader(stage);
    shader->setStrings(&shaderSource, 1);
    shader->setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader->setEnvTarget(glslang::EshTargetSpv, glslang::EShTargetSpv_1_5);

    const TBuiltInResource* resources = GetDefaultResources();

    glslang::TShader::Includer* includer = new BasicIncluder;

    printf("Parsing shader %s... ", fileName);
    shader->parse(resources, 100, false, EShMsgDefault, *includer);
    printf("%s\n", shader->getInfoLog());

    glslang::TProgram* program = new glslang::TProgram;
    program->addShader(shader);

    printf("Linking shader %s... ", fileName);
    program->link(EShMsgDefault);

    printf("%s\n", program->getInfoLog());

    if (!program->buildReflection()) {
        printf("Failed to build reflection\n");
    }

    return program;
}

static const char* s_shaderHeaderPrelude = R"(#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
)";

static const char* s_shaderHeaderPostlude = R"(#ifdef __cplusplus
}
#endif
)";

struct HeaderGenerator {
    glslang::TProgram* program;

    std::string structPrefix;
    std::string globalPrefix;
    std::string shaderName;
    EShLanguage stage;

    HeaderGenerator(glslang::TProgram* program, const Args& args) : program(program) {
        if (args.structPrefix) {
            structPrefix = *args.structPrefix;
        } else {
            structPrefix = "";
        }

        if (args.globalPrefix) {
            globalPrefix = *args.globalPrefix;
        } else {
            globalPrefix = "";
        }

        size_t lastSlash = args.inputFile.find_last_of("/\\");
        size_t lastDot = args.inputFile.find_last_of(".");
        if (lastDot == std::string::npos) {
            lastDot = args.inputFile.size();
        }

        shaderName = args.inputFile.substr(lastSlash + 1, lastDot - lastSlash - 1);

        stage = args.stage;
    }

    void generate(std::ofstream& outFile) {
        outFile << s_shaderHeaderPrelude;

        outFile << "static const uint32_t " << globalPrefix << shaderName << "_spv[] = {\n";

        glslang::TIntermediate* intermediate = program->getIntermediate(stage);

        if (!intermediate) {
            printf("Failed to get intermediate for stage %d\n", stage);
            return;
        }

        std::vector<unsigned int> spirv;
        glslang::GlslangToSpv(*intermediate, spirv);

        for (size_t j = 0; j < spirv.size(); j++) {
            if (j % 8 == 0) {
                outFile << "    ";
            }
            outFile << spirv[j];
            if (j != spirv.size() - 1) {
                outFile << ",";
            }

            if (j % 8 == 7) {
                outFile << "\n";
            }
        }

        outFile << "};\n";

        outFile << "static const size_t " << globalPrefix << shaderName
                << "_spv_size = " << spirv.size() << ";\n";

        outFile << "static const char* " << globalPrefix << shaderName << "_name = \""
                << shaderName << "\";\n";

        std::unordered_set<std::string> handledUniforms;
        std::unordered_map<std::string, const glslang::TType&> structsEncountered;

        // Gather uniform block info
        for (int i = 0; i < program->getNumUniformBlocks(); i++) {
            const glslang::TObjectReflection& uniformBlock = program->getUniformBlock(i);

            std::string uniformBlockName = uniformBlock.name;

            const glslang::TType& blockType = *uniformBlock.getType();

            if (blockType.getBasicType() == glslang::EbtBlock) {
                structsEncountered.insert({ uniformBlockName, blockType });
                const glslang::TTypeList* members = blockType.getStruct();

                for (size_t j = 0; j < members->size(); j++) {
                    std::string memberName = members->at(j).type->getFieldName().c_str();
                    handledUniforms.insert(memberName);

                    const glslang::TType& memberType = *members->at(j).type;

                    if (memberType.isStruct()) {
                        structsEncountered.insert({ memberType.getTypeName().c_str(),
                                                    memberType });
                    }
                }
            }

            outFile << "#define UNIFORM_" << uniformBlockName << " "
                    << uniformBlock.getBinding() << "\n";
        }

        // Gather buffer block info
        for (int i = 0; i < program->getNumBufferBlocks(); i++) {
            const glslang::TObjectReflection& bufferBlock = program->getBufferBlock(i);

            std::string bufferBlockName = bufferBlock.name;

            const glslang::TType& blockType = *bufferBlock.getType();

            if (blockType.getBasicType() == glslang::EbtBlock) {
                structsEncountered.insert({ bufferBlockName, blockType });
                const glslang::TTypeList* members = blockType.getStruct();

                for (size_t j = 0; j < members->size(); j++) {
                    std::string memberName = members->at(j).type->getFieldName().c_str();
                    if (bufferBlock.name.empty()) {
                        handledUniforms.insert(memberName);
                    } else {
                        handledUniforms.insert(bufferBlockName + "." + memberName);
                    }

                    const glslang::TType& memberType = *members->at(j).type;

                    if (memberType.isStruct()) {
                        structsEncountered.insert({ memberType.getTypeName().c_str(),
                                                    memberType });
                    }
                }
            }
            outFile << "#define BUFFER_" << bufferBlockName << " " << bufferBlock.getBinding()
                    << "\n";
        }

        // Generate location and binding defines
        for (int i = 0; i < program->getNumPipeInputs(); i++) {
            const glslang::TObjectReflection& input = program->getPipeInput(i);

            outFile << "#define SLOT_" << input.name << " " << input.layoutLocation() << "\n";
        }

        for (int i = 0; i < program->getNumUniformVariables(); i++) {
            const glslang::TObjectReflection& uniform = program->getUniform(i);

            if (handledUniforms.find(uniform.name) != handledUniforms.end()) {
                continue;
            }

            outFile << "#define UNIFORM_" << uniform.name << " " << uniform.getBinding()
                    << "\n";

            const glslang::TType& type = *uniform.getType();

            if (type.getBasicType() == glslang::EbtStruct) {
                structsEncountered.insert({ uniform.name, type });
            }
        }

        for (auto& [uniformBlockName, uniformBlock] : structsEncountered) {
            outFile << "typedef struct " << structPrefix << uniformBlockName << " "
                    << structPrefix << uniformBlockName << ";\n";
        }

        for (auto& [uniformBlockName, uniformBlock] : structsEncountered) {
            generateStruct(uniformBlockName, uniformBlock, outFile);
        }

        outFile << s_shaderHeaderPostlude;
    }

    int sizeOfType(const glslang::TType& type) {
        int size = 0;
        switch (type.getBasicType()) {
            case glslang::EbtFloat:
                size = 4;
                break;
            case glslang::EbtInt:
                size = 4;
                break;
            case glslang::EbtUint:
                size = 4;
                break;
            case glslang::EbtBool:
                size = 4;
                break;
            case glslang::EbtStruct: {
                size = 0;
                const glslang::TTypeList* members = type.getStruct();
                for (size_t j = 0; j < members->size(); j++) {
                    size += sizeOfType(*members->at(j).type);
                }
                break;
            }
            default:
                size = 0;
        }

        if (type.isVector()) {
            size *= type.getVectorSize();
        } else if (type.isArray()) {
            if (type.isSizedArray()) {
                size *= type.getOuterArraySize();
            }
        } else if (type.isMatrix()) {
            size *= type.getMatrixCols() * type.getMatrixRows();
        }

        return size;
    }

    int getsPaddedTo(const glslang::TType& type) {
        int padding = 0;
        switch (type.getBasicType()) {
            case glslang::EbtFloat:
                padding = 4;
                break;
            case glslang::EbtInt:
                padding = 4;
                break;
            case glslang::EbtUint:
                padding = 4;
                break;
            case glslang::EbtBool:
                padding = 4;
                break;
            case glslang::EbtStruct: {
                padding = 0;
                const glslang::TTypeList* members = type.getStruct();
                for (size_t j = 0; j < members->size(); j++) {
                    padding += getsPaddedTo(*members->at(j).type);
                }
                break;
            }
            default:
                padding = 0;
        }

        if (type.isVector()) {
            if (type.getVectorSize() == 3) {
                padding = 16;
            } else {
                padding *= type.getVectorSize();
            }
        } else if (type.isMatrix()) {
            padding = 16;
        }

        return padding;
    }

    void generateStruct(
        const std::string& structName,
        const glslang::TType& structType,
        std::ofstream& outFile
    ) {
        const glslang::TTypeList* members = structType.getStruct();

        outFile << "/// Struct for " << structName << "\n";
        std::stringstream structString;
        structString << "{\n";

        int sizeSoFar = 0;
        int paddingCounter = 0;
        int maxPadding = 0;

        for (size_t j = 0; j < members->size(); j++) {
            const glslang::TType& memberType = *members->at(j).type;

            std::string memberName = members->at(j).type->getFieldName().c_str();
            std::string typeString = getFieldString(memberType, memberName);

            int padding = getsPaddedTo(memberType);
            if (padding > maxPadding) {
                maxPadding = padding;
            }

            if (sizeSoFar % padding != 0) {
                int paddingNeeded = padding - (sizeSoFar % padding);
                structString << "    uint8_t _padding" << paddingCounter++ << "["
                             << paddingNeeded << "];\n";
                sizeSoFar += paddingNeeded;
            }

            structString << "    " << typeString << ";\n";

            sizeSoFar += sizeOfType(memberType);
        }

        bool lastElementIsUnboundedArray =
            members->size() > 0 && members->at(members->size() - 1).type->isArray() &&
            !members->at(members->size() - 1).type->isSizedArray();

        if (sizeSoFar % maxPadding != 0 && !lastElementIsUnboundedArray) {
            int paddingNeeded = maxPadding - (sizeSoFar % maxPadding);
            structString << "    uint8_t _padding" << paddingCounter++ << "[" << paddingNeeded
                         << "];\n";
            sizeSoFar += paddingNeeded;
        }

        structString << "}";

        outFile << "typedef struct " << structPrefix << structName << " " << structString.str()
                << " " << structPrefix << structName << ";\n";
    }

    std::string getFieldString(const glslang::TType& type, const std::string& name) {
        std::string fieldString;
        switch (type.getBasicType()) {
            case glslang::EbtFloat:
                fieldString = "float";
                break;
            case glslang::EbtInt:
                fieldString = "int";
                break;
            case glslang::EbtUint:
                fieldString = "uint";
                break;
            case glslang::EbtBool:
                fieldString = "bool";
                break;
            case glslang::EbtStruct:
                fieldString = structPrefix + type.getTypeName().c_str();
                break;
            default:
                return "unknown";
        }

        if (type.isVector()) {
            fieldString += " " + name + "[" + std::to_string(type.getVectorSize()) + "]";
        } else if (type.isArray()) {
            if (type.isSizedArray()) {
                fieldString +=
                    " " + name + "[" + std::to_string(type.getOuterArraySize()) + "]";
            } else {
                fieldString += " " + name + "[]";
            }
        } else if (type.isMatrix()) {
            fieldString += " " + name + "[" + std::to_string(type.getMatrixCols()) + "]";
        } else {
            fieldString += " " + name;
        }

        return fieldString;
    }
};

int main(int argc, char* argv[]) {
    Args args(argc, argv);

    glslang::InitializeProcess();

    std::ifstream file(args.inputFile);
    if (!file.is_open()) {
        printf("Failed to open file %s\n", args.inputFile.c_str());
        return 1;
    }

    std::string shaderSource(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    glslang::TProgram* program =
        CompileShader(shaderSource.c_str(), args.inputFile.c_str(), args.stage);
    if (!program) {
        return 1;
    }

    std::ofstream outFile(args.outputFile);
    if (!outFile.is_open()) {
        printf("Failed to open output file %s\n", args.outputFile.c_str());
        return 1;
    }

    HeaderGenerator headerGen(program, args);

    headerGen.generate(outFile);

    outFile.close();

    delete program;

    glslang::FinalizeProcess();

    return 0;
}

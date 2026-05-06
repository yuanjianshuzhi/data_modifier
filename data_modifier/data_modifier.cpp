#include <iostream>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>
#include <set>
#include <regex>   // 新增，用于解析标准 INCLUDE 格式

const size_t BUF_SIZE = 4096;

// 递归收集所有被 INCLUDE 的文件（绝对路径）
void collectIncludeFiles(const std::filesystem::path& filePath,
                         const std::filesystem::path& baseDir,
                         std::set<std::string>& allFiles) {
    if (!std::filesystem::exists(filePath) || !std::filesystem::is_regular_file(filePath))
        return;

    std::ifstream file(filePath);
    if (!file.is_open()) return;

    std::string line;
    bool in_include = false;  // 用于旧格式：单独一行 "INCLUDE"，下一行为文件名
    while (std::getline(file, line)) {
        // 跳过空行和注释（Eclipse 注释以 -- 开头）
        if (line.empty() || (line.size() >= 2 && line[0] == '-' && line[1] == '-'))
            continue;

        // 尝试匹配标准格式：INCLUDE 'filename' 或 INCLUDE "filename"
        std::regex include_regex(R"(INCLUDE\s+['\"]([^'\"]+)['\"])", std::regex::icase);
        std::smatch match;
        if (std::regex_search(line, match, include_regex) && match.size() >= 2) {
            std::string incFile = match[1].str();
            std::filesystem::path incPath = baseDir / incFile;
            std::string absPath = std::filesystem::absolute(incPath).string();
            if (allFiles.find(absPath) == allFiles.end()) {
                allFiles.insert(absPath);
                collectIncludeFiles(incPath, baseDir, allFiles);
            }
            continue;
        }

        // 旧格式：单独一行 "INCLUDE"，下一行为带引号的文件名
        if (in_include) {
            size_t pos = line.find('\'');
            size_t rpos = line.rfind('\'');
            if (pos != std::string::npos && rpos != std::string::npos && rpos > pos) {
                std::string incFile = line.substr(pos + 1, rpos - pos - 1);
                std::filesystem::path incPath = baseDir / incFile;
                std::string absPath = std::filesystem::absolute(incPath).string();
                if (allFiles.find(absPath) == allFiles.end()) {
                    allFiles.insert(absPath);
                    collectIncludeFiles(incPath, baseDir, allFiles);
                }
            }
            in_include = false;
        }

        // 检测是否是旧格式的 "INCLUDE" 行（前后可带空格）
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        if (trimmed == "INCLUDE") {
            in_include = true;
        }
    }
    file.close();
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <plan_json_path>" << std::endl;
        return EXIT_FAILURE;
    }
    std::string planJson = argv[1];

    //std::string planJson = "D:/plan/param_plan2/action1.json";
    std::filesystem::path planPath(planJson);
    std::filesystem::path dir  = planPath.parent_path();
    std::filesystem::path fDir = planPath.parent_path().parent_path();

    if (!std::filesystem::exists(planJson) || !std::filesystem::is_regular_file(planJson)) {
        std::cerr << "Error: 配置文件不存在或非普通文件！" << std::endl;
         return EXIT_FAILURE;
    }

    std::ifstream inFile(planJson);
    if (!inFile.is_open()) {
        std::cerr << "Json文件打开失败！" << std::endl;
		return EXIT_FAILURE;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(inFile);
    }
    catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON解析失败！错误位置：" << e.byte << " 错误信息：" << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    const std::string KEY_PLAN_ID = "plan_id";
    const std::string KEY_MODEL_FILE = "model_file";
    const std::string KEY_MODIFICATION = "modifications";
    const std::string KEY_TARGET_FILE = "target_file";
    const std::string KEY_OPERATION = "operation";
    const std::string KEY_ORIGINAL = "original";
    const std::string KEY_MODIFIED = "modified";

    //file >> data; // 直接用流读取，自动解析
    if (!data.contains(KEY_PLAN_ID) || !data.at(KEY_PLAN_ID).is_string())
    {
        std::cerr << "Error: JSON缺少有效的 'plan_id' 字段！" << std::endl;
        return EXIT_FAILURE;
	}
    if (!data.contains(KEY_MODEL_FILE) || !data.at(KEY_MODEL_FILE).is_string())
    {
        std::cerr << "Error: JSON缺少有效的 'model_file' 字段！" << std::endl;
        return EXIT_FAILURE;
    }
    if (!data.contains(KEY_MODIFICATION) || !data.at(KEY_MODIFICATION).is_array())
    {
        std::cerr << "Error: JSON缺少有效的 'modification' 数组字段！" << std::endl;
		return EXIT_FAILURE;
    }

    std::filesystem::path modelFilePath = fDir / data[KEY_MODEL_FILE].get<std::string>();
    std::ifstream model_file(modelFilePath);
    if (!model_file.is_open()) {
        std::cerr << "模型文件打开失败！" << modelFilePath << std::endl;
        return EXIT_FAILURE;
    }

    // ---- 收集所有需要复制的文件（主模型 + 所有嵌套 INCLUDE）----
    std::set<std::string> allFiles;
    std::string modelAbsPath = std::filesystem::absolute(modelFilePath).string();
    allFiles.insert(modelAbsPath);
    collectIncludeFiles(modelFilePath, fDir, allFiles);

    // 复制主模型文件到目标目录
    std::filesystem::path modelFileDesPath = dir / data[KEY_MODEL_FILE].get<std::string>();
    std::filesystem::create_directories(modelFileDesPath.parent_path());
    std::filesystem::copy(modelFilePath, modelFileDesPath, std::filesystem::copy_options::overwrite_existing);

    // 复制所有 INCLUDE 文件（保持相对路径）
    for (const auto& absPath : allFiles) {
        if (absPath == modelAbsPath) continue; // 已复制
        std::filesystem::path src(absPath);
        // 计算相对于 fDir 的路径
        std::filesystem::path rel = std::filesystem::relative(src, fDir);
        std::filesystem::path dest = dir / rel;
        std::filesystem::create_directories(dest.parent_path());
        std::filesystem::copy(src, dest, std::filesystem::copy_options::overwrite_existing);
    }
    model_file.close();

    // 原批量复制其他文件的代码（注释状态，保持不变）
   // for (const auto& entry : std::filesystem::directory_iterator(fDir))
   // {
   //     const auto& path = entry.path();
   //     if (std::filesystem::is_regular_file(entry.status()))
   //     {
			//std::string substr = path.filename().string().substr(path.filename().string().rfind("."));
   //         std::transform(substr.begin(), substr.end(), substr.begin(),
   //             [](unsigned char c) { return std::toupper(c); });
   //         if (!(substr == ".DESCRIBE") && !(substr == ".SMSPEC") &&
   //             !(substr == ".UNRST") && !(substr == ".UNSMRY") && !(substr == ".XJSON") &&
   //             !(substr == ".JSON") && !(substr == ".MD") && !(substr == ".EXE"))
   //         {
   //             std::filesystem::path destination = dir / path.filename().string();
   //             std::filesystem::copy(path, destination, std::filesystem::copy_options::overwrite_existing);
   //         }
   //     }
   // }

    // 后续的修改操作（原代码保持不变）
    for (const auto& mod : data[KEY_MODIFICATION])
    {
        if (!mod.contains(KEY_TARGET_FILE) || !mod.at(KEY_TARGET_FILE).is_string())
        {
            std::cerr << "Error: JSON中的 'modification' 数组元素缺少有效的 'target_file' 字段！" << std::endl;
            return EXIT_FAILURE;
        }
        if (!mod.contains(KEY_OPERATION) || !mod.at(KEY_OPERATION).is_string())
        {
            std::cerr << "Error: JSON中的 'modification' 数组元素缺少有效的 'operation' 字段！" << std::endl;
            return EXIT_FAILURE;
        }
        if (!mod.contains(KEY_ORIGINAL) || !mod.at(KEY_ORIGINAL).is_string())
        {
            std::cerr << "Error: JSON中的 'modification' 数组元素缺少有效的 'original' 字段！" << std::endl;
            return EXIT_FAILURE;
        }
        if (!mod.contains(KEY_MODIFIED) || !mod.at(KEY_MODIFIED).is_string())
        {
            std::cerr << "Error: JSON中的 'modification' 数组元素缺少有效的 'modified' 字段！" << std::endl;
            return EXIT_FAILURE;
        }
        std::string target_file_str = mod[KEY_TARGET_FILE].get<std::string>();
        std::filesystem::path targetPath = dir / target_file_str;
        std::filesystem::path tmpPath = dir / (target_file_str + ".tmp");

        // 1. 打开原文件（只读二进制）
        std::ifstream in_file(targetPath);
        if (!in_file.is_open()) {
            std::cerr << "原文件打开失败！" << targetPath<< std::endl;
            return EXIT_FAILURE;
        }

        // 2. 创建临时新文件（只写二进制）
        std::ofstream out_file(tmpPath);
        if (!out_file.is_open()) {
            std::cerr << "临时文件创建失败！" << std::endl;
            return EXIT_FAILURE;
        }

        std::string operation = mod[KEY_OPERATION];
        std::string original = mod[KEY_ORIGINAL];
        std::string modified = mod[KEY_MODIFIED];

        // 2. 流式读取 + 跨块匹配（防止字符串被缓冲区截断）
        char buf[BUF_SIZE];
        // 保留上一块末尾的 (target_len-1) 个字符，处理跨块匹配
        std::string leftover;
        std::streampos read_pos = 0; // 当前读取位置

        while (in_file.read(buf, BUF_SIZE) || in_file.gcount() > 0)
        {
            size_t bytes_read = in_file.gcount();
            // 拼接：上次剩余 + 当前块
            std::string current = leftover + std::string(buf, bytes_read);
            size_t pos = 0;
            size_t last_pos = 0;

            // 循环查找所有目标
            while ((pos = current.find(original, last_pos)) != std::string::npos) {
                // 写入目标之前的原数据
                out_file.write(current.data() + last_pos, pos - last_pos);
                // 写入替换串（不等长直接写）
                out_file.write(modified.data(), modified.size());
                last_pos = pos + original.length();
            }

            // 写入最后一段未匹配的数据（保留末尾用于跨块）
            size_t keep_len = (current.size() >= original.length()) ? (original.length() - 1) : 0;
            out_file.write(current.data() + last_pos, current.size() - last_pos - keep_len);
            // 更新剩余字符
            leftover = current.substr(current.size() - keep_len);
        }
        if (!leftover.empty()) {
            out_file.write(leftover.data(), leftover.size());
        }
        in_file.close();
        out_file.close();

		bool remove_result = std::filesystem::remove(targetPath);
		size_t retry_count = 0;
        while (!remove_result)
        {
            if (!(remove_result = std::filesystem::remove(targetPath)))
                retry_count++;
			else
				break;
            if(retry_count == 10)
            {
                std::cerr << "删除原文件失败！" << std::endl;
                return EXIT_FAILURE;
			}
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, targetPath, ec);
        if (ec) {
            std::cerr << "重命名失败！临时文件：" << tmpPath.string() << " 目标文件：" << targetPath.string()
                << " 错误码：" << ec.value() << " 错误信息：" << ec.message() << std::endl;
            return EXIT_FAILURE;
        }
    }
}
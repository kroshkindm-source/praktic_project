#pragma once
#include <cstdio>
#include <fstream> 
#include <sstream>
#include <stdexcept>
#include <string>

std::string readTextFile(const std::string& path);
std::string sanitizeUtf8(const std::string& input);
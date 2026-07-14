#pragma once 
#ifndef OCR_LOGIC_H
#define OCR_LOGIC_H

#include <string>
#include "DocumentDescription.h"

std::string ocrLogic(int docId, const std::string& imagePath, DocumentDescriptionRepository& repo);

#endif
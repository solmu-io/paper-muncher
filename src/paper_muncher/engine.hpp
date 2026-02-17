#pragma once
#include <string>
#include <vector>

namespace SolPDF {

struct Options {
    std::string output_format;
    std::string paper;
    std::string orientation;
    double width  = 0;
    double height = 0;
    double scale  = 1.0;
    double density = 1.0;
    std::string background;
    // Add more fields as needed
};

// Initialize global resources (fonts, renderer, etc.)
void init();

// Shutdown global resources
void shutdown();

// Convert HTML string → PDF buffer
std::vector<unsigned char> html_to_pdf(
    const std::string& html,
    const Options& opts
);

} // namespace SolPDF

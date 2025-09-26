# FontIcon Usage Collector Tools

This directory contains tools for analyzing and visualizing FontIcon usage in the OpenCloud Desktop codebase.

## Overview

The FontIcon class in `src/resources/fonticon.h` is used throughout the codebase to display font-based icons from FontAwesome and RemixIcon families. These tools help developers understand how icons are used across the application.

## Tools Provided

### 1. Console Analysis Tool (`fonticon_collector_console`)
- **Purpose**: Command-line analysis of FontIcon usage
- **Output**: Detailed text report with all FontIcon instances
- **Use case**: Automated analysis, CI/CD integration, quick overview

### 2. GUI Analysis Tool (`fonticon_collector_simple`) 
- **Purpose**: Interactive visual analysis with glyph preview
- **Output**: GUI application showing list of usages with glyph rendering
- **Use case**: Visual inspection, design review, icon inventory

## Quick Start

Run the convenience script:
```bash
./run_fonticon_tools.sh
```

This will build both tools and run them sequentially.

## Manual Building

### Console Version
```bash
mkdir build_console && cd build_console
cp ../CMakeLists_console.txt ./CMakeLists.txt
cp ../fonticon_collector_console.cpp .
cmake . && make
./fonticon_collector_console
```

### GUI Version
```bash
mkdir build_simple && cd build_simple  
cp ../CMakeLists_simple.txt ./CMakeLists.txt
cp ../fonticon_collector_simple.cpp .
cmake . && make
./fonticon_collector
```

## Sample Output

The tools currently find **27 FontIcon usages** across the codebase:

### Most Common Glyphs
- ⚠️ Warning icon (`U+F071`) - notifications, dialogs
- ℹ️ Info icon (`U+F05A`) - information messages  
- 🔄 Sync icon (`U+F021`) - sync operations
- 📁 Folder icon (`U+F07B`) - file/folder representations

### Font Families Used
- **FontAwesome**: 24 instances (89%)
- **RemixIcon**: 3 instances (11%)

### Icon Sizes
- **Normal**: 24 instances (89%)
- **Half**: 3 instances (11%)

## File Analysis

The tools scan these file types:
- `*.cpp` - C++ source files
- `*.h` - C++ header files

### Parsing Logic
1. **Constructor Detection**: Regex pattern `FontIcon\s*\([^)]*\)`
2. **Glyph Extraction**: Supports multiple formats:
   - `u'X'` - Single character literals
   - `u'\uXXXX'` - Unicode hex escapes
   - Direct character literals
3. **Metadata Extraction**: Family and size enums via regex

## GUI Features

The GUI application provides:
- **Usage List**: All FontIcon instances with file/line info
- **Glyph Preview**: Visual rendering of each icon
- **Detailed View**: Unicode values, family, size, source context
- **Source Context**: Code snippet showing usage in context

## Integration with Main Build

To integrate with the main OpenCloud build system, update `src/cmd/CMakeLists.txt`:

```cmake
# FontIcon Collector Tool  
add_executable(fonticon_collector
    fonticon_collector.cpp
)
set_target_properties(fonticon_collector PROPERTIES OUTPUT_NAME "fonticon_collector")

target_link_libraries(fonticon_collector 
    OpenCloudResources 
    Qt::Core 
    Qt::Widgets 
    Qt::Gui
)
apply_common_target_settings(fonticon_collector)
```

## Dependencies

### Console Tool
- Qt6Core

### GUI Tool  
- Qt6Core
- Qt6Widgets
- Qt6Gui

## Future Enhancements

Potential improvements:
1. **Export Options**: CSV, JSON, HTML reports
2. **Icon Duplication Detection**: Find redundant icon usage
3. **Missing Icon Detection**: Icons referenced but not found
4. **Usage Statistics**: Frequency analysis, unused icon detection
5. **Integration**: IDE plugins, VS Code extension
6. **Theme Analysis**: Color usage patterns per icon

## Troubleshooting

### "Could not find src directory"
Make sure to run the tools from the OpenCloud Desktop root directory.

### GUI doesn't start
For headless environments, use:
```bash
xvfb-run -a ./fonticon_collector
```

### Build errors
Ensure Qt6 development packages are installed:
```bash
sudo apt install qt6-base-dev qt6-tools-dev
```
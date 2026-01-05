#!/usr/bin/env python3
"""Generate CMake source list from 7za.dll.vcxproj"""

import re
import sys

vcxproj_path = "src/plugins/7zip/vcxproj/7ZA/7za.dll.vcxproj"

with open(vcxproj_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Extract all ClCompile Include paths
pattern = r'ClCompile Include="([^"]+)"'
matches = re.findall(pattern, content)

print("set(7ZA_SOURCES")
for path in matches:
    # Convert backslashes to forward slashes
    path = path.replace('\\', '/')
    # Convert relative path to CMake variable
    path = path.replace('../../7za/', '${SEVENZIP_7ZA}/')
    print(f'  "{path}"')
print(")")

#!/bin/bash

# 指定输入和输出目录
SHADER_DIR="."
OUT_DIR="./spv"

# 如果不存在输出目录就创建
mkdir -p "$OUT_DIR"

for file in "$SHADER_DIR"/*.vert "$SHADER_DIR"/*.frag "$SHADER_DIR"/*.comp; do
    # 如果文件不存在就跳过（防止 *.shader.vert 不存在时报错）
    [ -e "$file" ] || continue

    filename=$(basename "$file")
    output="$OUT_DIR/${filename}.spv"

    echo "Compiling $file -> $output"
    glslc "$file" -o "$output"
done

echo "Done!"

#!/bin/bash
# znet 性能分析脚本

set -e

PORT=9000
THREADS=4
DURATION=30
BUILD_DIR=${BUILD_DIR:-build}

run_perf_test() {
    local prefix=$1
    
    # 创建输出目录
    local output_dir="perf_results/${prefix}_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$output_dir"
    echo "输出目录: $output_dir"
    
    echo ""
    echo "=========================================="
    echo "性能分析: ${prefix}"
    echo "=========================================="
    
    # 构建启动命令
    local cmd="${BUILD_DIR}/bin/znet_perf_server_bench -p $PORT -t $THREADS -d $DURATION"
    
    # 启动服务器
    echo "[1] 启动服务器: port=$PORT, threads=$THREADS, duration=$DURATION"
    $cmd > ${output_dir}/server.log 2>&1 &
    SERVER_PID=$!
    echo "服务器PID: $SERVER_PID"

    # 等待服务器启动
    sleep 2
    
    # 检查服务器是否运行
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo "错误: 服务器启动失败"
        cat ${output_dir}/server.log
        return 1
    fi
    
    echo "[2] 使用perf记录CPU性能数据 (18秒)..."
    perf record -F 99 -g -p $SERVER_PID -o ${output_dir}/perf_cpu.data -- sleep 18 2>${output_dir}/perf_record.err &
    PERF_PID=$!
    
    echo "[3] 使用perf stat记录缓存命中率..."
    perf stat -e cache-references,cache-misses,instructions,cycles,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses -p $SERVER_PID sleep 18 2>${output_dir}/perf_stat.txt &
    STAT_PID=$!
    
    # 等待1秒让perf开始记录
    sleep 1
    
    echo "[4] 运行wrk压测..."
    wrk -t4 -c200 -d15s http://localhost:$PORT/ > ${output_dir}/wrk_result.txt 2>&1 || echo "wrk执行完成"
    
    # 等待perf完成
    echo "[5] 等待perf记录完成..."
    wait $PERF_PID 2>/dev/null || true
    wait $STAT_PID 2>/dev/null || true
    
    echo "[6] 等待服务器结束..."
    wait $SERVER_PID 2>/dev/null || true

    echo ""
    echo "=========================================="
    echo "性能分析报告 - $prefix"
    echo "=========================================="
    
    echo ""
    echo "=== WRK压测结果 ==="
    cat ${output_dir}/wrk_result.txt
    
    echo ""
    echo "=== Perf Stat缓存统计 ==="
    cat ${output_dir}/perf_stat.txt 2>/dev/null || echo "perf stat数据收集失败"
    
    echo ""
    echo "=== 生成perf性能数据 ==="
    if [ -f ${output_dir}/perf_cpu.data ]; then
        perf report -i ${output_dir}/perf_cpu.data -n --stdio --sort symbol,dso 2>/dev/null > ${output_dir}/perf_functions.txt || echo "perf report生成失败"
        echo "函数性能数据 (前100行):"
        head -100 ${output_dir}/perf_functions.txt
    else
        echo "perf_cpu.data文件不存在"
    fi
    
    echo ""
    echo "=========================================="
    echo "分析文件已生成到目录: $output_dir"
    echo "=========================================="
    echo "数据文件:"
    echo "  - perf_cpu.data: CPU性能数据"
    echo "  - perf_stat.txt: 缓存统计"
    echo ""
    echo "报告文件:"
    echo "  - perf_functions.txt: 所有函数性能数据"
    echo "  - wrk_result.txt: 压测结果"
    echo "  - server.log: 服务器日志"
    echo "=========================================="
}

# 检查依赖
check_dependencies() {
    echo "检查依赖工具..."
    
    if ! command -v perf &> /dev/null; then
        echo "错误: 未找到perf命令"
        echo "安装: sudo apt-get install linux-tools-common linux-tools-generic linux-tools-\$(uname -r)"
        exit 1
    fi
    
    if ! command -v wrk &> /dev/null; then
        echo "错误: 未找到wrk命令"
        echo "安装: sudo apt-get install wrk"
        exit 1
    fi
    
    if [ ! -f "${BUILD_DIR}/bin/znet_perf_server_bench" ]; then
        echo "错误: 未找到 ${BUILD_DIR}/bin/znet_perf_server_bench"
        echo "请先编译项目: cmake -B build && cmake --build build"
        exit 1
    fi
    
    echo "依赖检查通过"
}

print_usage() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -p <port>      服务器端口 (default: 9000)"
    echo "  -t <threads>   工作线程数 (default: 4)"
    echo "  -d <duration>  运行时长(秒) (default: 30)"
    echo "  -b <dir>       构建目录 (default: build)"
    echo "  -h             显示帮助"
    echo ""
    echo "示例:"
    echo "  $0                          # 使用默认参数"
    echo "  $0 -p 8080 -t 8 -d 60       # 自定义参数"
    echo "  $0 -b build_release         # 指定构建目录"
}

# 解析命令行参数
while getopts "p:t:d:b:h" opt; do
    case $opt in
        p)
            PORT=$OPTARG
            ;;
        t)
            THREADS=$OPTARG
            ;;
        d)
            DURATION=$OPTARG
            ;;
        b)
            BUILD_DIR=$OPTARG
            ;;
        h)
            print_usage
            exit 0
            ;;
        *)
            print_usage
            exit 1
            ;;
    esac
done

# 主流程
echo "=========================================="
echo "znet 性能分析工具"
echo "=========================================="
echo "配置:"
echo "  端口: $PORT"
echo "  线程数: $THREADS"
echo "  运行时长: $DURATION 秒"
echo "  构建目录: $BUILD_DIR"
echo "=========================================="
echo ""

check_dependencies

run_perf_test "znet"

echo ""
echo "=========================================="
echo "性能分析完成"
echo "=========================================="

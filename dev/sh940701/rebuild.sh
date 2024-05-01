#!/bin/bash

# 첫 번째 인자를 filePath 변수에 저장
filePath=$1
# 두 번째 인자를 testName 변수에 저장
testName=$2
# 세 번째 인자를 debugMode 변수에 저장
debugMode=$3

# filePath로 지정된 디렉토리의 build 폴더로 이동 후 make clean 실행
cd ${filePath}/build && make clean

# 이전 명령이 성공적으로 실행되면, 같은 디렉토리에서 make 실행
if [ $? -eq 0 ]; then
    make
    # make 실행 성공 시, pintos 명령어 실행
    if [ $? -eq 0 ]; then
        # debugMode가 1일 경우 --gdb 옵션을 포함하여 pintos 실행
        if [ "$debugMode" -eq 1 ]; then
            pintos --gdb -- -q run $testName
        else
            pintos -- -q run $testName
        fi
    else
        echo "make 실행 중 오류가 발생했습니다."
    fi
else
    echo "디렉토리 이동이나 make clean 실행 중 오류가 발생했습니다."
fi
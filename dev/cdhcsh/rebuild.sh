#!/bin/bash

# 첫 번째 인자를 filePath 변수에 저장
filePath=$1
testName=$2

# filePath로 지정된 디렉토리의 build 폴더로 이동
cd ${filePath}/build && make clean

# 이전 명령이 성공적으로 실행되면, 같은 디렉토리에서 make 실행
if [ $? -eq 0 ]; then
    make
    pintos -- -q run $2
else
    echo "디렉토리 이동이나 make clean 실행 중 오류가 발생했습니다."
fi` `
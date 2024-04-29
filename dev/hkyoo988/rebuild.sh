#!/bin/bash

# 첫 번째 인자를 filePath 변수에 저장
filePath=threads

# filePath로 지정된 디렉토리의 build 폴더로 이동
cd ${filePath}/build && make clean

# 이전 명령이 성공적으로 실행되면, 같은 디렉토리에서 make 실행
if [ $? -eq 0 ]; then
    make
else
    cd ${filePath} && make && cd build
fi

pintos -- -q run alarm-multiple
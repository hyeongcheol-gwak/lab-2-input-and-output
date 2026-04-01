#!/bin/bash

# 실행 파일 경로 설정 (Makefile이 있는 프로젝트 루트 기준)
DIRTREE="./dirtree"

# 실행 파일이 존재하는지 확인
if [ ! -f "$DIRTREE" ]; then
    echo "오류: $DIRTREE 실행 파일을 찾을 수 없습니다. 먼저 'make' 명령어로 컴파일해주세요."
    exit 1
fi

# 임시 디렉토리 생성
TEST_DIR="test_results"
mkdir -p "$TEST_DIR"

# 테스트 실행 및 비교 함수
run_test() {
    local test_name="$1"
    local command="$2"
    local expected_file="$3"
    local actual_file="${TEST_DIR}/${test_name// /_}_actual.txt"

    echo "==================================================="
    echo "▶ 실행 중: $test_name"
    echo "▶ 명령어: $command"
    
    # 명령어 실행 (표준 출력과 에러 모두 캡처)
    eval "$command" > "$actual_file" 2>&1

    # diff로 결과 비교 (공백 무시 옵션 -b 적용)
    if diff -b "$expected_file" "$actual_file" > /dev/null; then
        echo "✅ 결과: PASS (정답과 일치합니다)"
    else
        echo "❌ 결과: FAIL (출력이 다릅니다)"
        echo "--- [Diff 결과 ( < 예상 정답 / > 실제 출력 )] ---"
        diff -u "$expected_file" "$actual_file"
    fi
    echo "==================================================="
    echo ""
}

# ---------------------------------------------------------
# 정답 파일(Expected Outputs) 생성
# ---------------------------------------------------------

# Test 1: 기본 디렉토리 순회
cat << 'EOF' > "$TEST_DIR/expected_test1.txt"
Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
demo
  subdir1                                                   root:root            4096         8    d
    simplefile                                              root:root             256         8     
    sparsefile                                              root:root            8192         8     
    thisisanextremelyveryverylongfilenameforsuchasi...      root:root            1000         8     
  subdir2                                                   root:root            4096         8    d
    brokenlink                                              root:root               8         0    l
    symboliclink                                            root:root               6         0    l
    textfile1.txt                                           root:root            1024         8     
    textfile2.txt                                           root:root            2048         8     
  subdir3                                                   root:root            4096         8    d
    code1.c                                                 root:root             200         8     
    code2.c                                                 root:root             300         8     
    pipe                                                    root:root               0         0    f
    socket                                                  root:root               0         0    s
  one                                                       root:root               1         8     
  two                                                       root:root               2         8     
----------------------------------------------------------------------------------------------------
9 files, 3 directories, 2 links, 1 pipe, and 1 socket                           25325        96
EOF

# Test 2: 다중 디렉토리 및 집계 통계
cat << 'EOF' > "$TEST_DIR/expected_test2.txt"
Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
demo/subdir2
  brokenlink                                                root:root               8         0    l
  symboliclink                                              root:root               6         0    l
  textfile1.txt                                             root:root            1024         8     
  textfile2.txt                                             root:root            2048         8     
----------------------------------------------------------------------------------------------------
2 files, 0 directories, 2 links, 0 pipes, and 0 sockets                          3086        16

Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
demo/subdir3
  code1.c                                                   root:root             200         8     
  code2.c                                                   root:root             300         8     
  pipe                                                      root:root               0         0    f
  socket                                                    root:root               0         0    s
----------------------------------------------------------------------------------------------------
2 files, 0 directories, 0 links, 1 pipe, and 1 socket                             500        16

Analyzed 2 directories:
  total # of files:                       4
  total # of directories:                 0
  total # of links:                       2
  total # of pipes:                       1
  total # of sockets:                     1
  total # of entries:                     8
  total file size:                     3586
  total # of blocks:                     32
EOF

# Test 3: 깊이 제한 (-d 2)
cat << 'EOF' > "$TEST_DIR/expected_test3.txt"
Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
test1/a
  b                                                         root:root            4096         8    d
    c                                                       root:root            4096         8    d
    f                                                       root:root               0         0     
----------------------------------------------------------------------------------------------------
1 file, 2 directories, 0 links, 0 pipes, and 0 sockets                           8192        16
EOF

# Test 4: 패턴 필터링 '?' (-f 'a?c')
cat << 'EOF' > "$TEST_DIR/expected_test4.txt"
Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
pat1
  subdir1
    aXc                                                     root:root               1         8     
  abc                                                       root:root               1         8     
  axc                                                       root:root               1         8     
----------------------------------------------------------------------------------------------------
3 files, 0 directories, 0 links, 0 pipes, and 0 sockets                             3        24
EOF

# Test 5: 패턴 필터링 그룹과 * (-f '(ab)*c')
cat << 'EOF' > "$TEST_DIR/expected_test5.txt"
Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
pat2
  outer
    inner
      ababc                                                 root:root               1         8     
  abababc                                                   root:root               1         8     
  ababc                                                     root:root               1         8     
  abc                                                       root:root               1         8     
  c                                                         root:root               1         8     
  xababcx                                                   root:root               1         8     
----------------------------------------------------------------------------------------------------
6 files, 0 directories, 0 links, 0 pipes, and 0 sockets                             6        48
EOF

# Test 6: 잘못된 패턴 오류 처리 (-f 'a**b')
cat << 'EOF' > "$TEST_DIR/expected_test6.txt"
Invalid pattern syntax
EOF

# Test 7: 존재하지 않는 파일 오류 처리
cat << 'EOF' > "$TEST_DIR/expected_test7.txt"
Name                                                        User:Group           Size    Blocks Type
----------------------------------------------------------------------------------------------------
b
  ERROR: No such file or directory
----------------------------------------------------------------------------------------------------
0 files, 0 directories, 0 links, 0 pipes, and 0 sockets                             0         0
EOF

# ---------------------------------------------------------
# 테스트 실행
# ---------------------------------------------------------

echo "==================================================="
echo "         dirtree 자동화 테스트 스크립트 시작         "
echo "==================================================="

# 테스트 전 경고 (디렉토리가 없으면 diff가 무조건 실패합니다)
if [ ! -d "demo" ] || [ ! -d "test1" ] || [ ! -d "pat1" ]; then
    echo "⚠️ 주의: 'demo', 'test1', 'pat1' 등의 디렉토리가 현재 위치에 없습니다."
    echo "⚠️ README에 안내된 대로 'tools/gentree.sh'를 사용하여 트리 구조를 먼저 생성하세요."
    echo ""
fi

run_test "Test 1 (Basic Output)" "$DIRTREE demo" "$TEST_DIR/expected_test1.txt"
run_test "Test 2 (Multiple Dirs & Aggregate)" "$DIRTREE demo/subdir2 demo/subdir3" "$TEST_DIR/expected_test2.txt"
run_test "Test 3 (Depth Limit: -d 2)" "$DIRTREE -d 2 test1/a" "$TEST_DIR/expected_test3.txt"
run_test "Test 4 (Regex '?': -f 'a?c')" "$DIRTREE -f 'a?c' pat1" "$TEST_DIR/expected_test4.txt"
run_test "Test 5 (Regex '()', '*': -f '(ab)*c')" "$DIRTREE -f '(ab)*c' pat2" "$TEST_DIR/expected_test5.txt"
run_test "Test 6 (Invalid Pattern)" "$DIRTREE -f 'a**b' demo" "$TEST_DIR/expected_test6.txt"
run_test "Test 7 (No File Error)" "$DIRTREE b" "$TEST_DIR/expected_test7.txt"

echo "모든 테스트가 완료되었습니다! 상세 결과는 '$TEST_DIR' 폴더를 확인하세요."
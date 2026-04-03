//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     Spring 2026
//
/// @file
/// @brief 디렉토리 트리를 재귀적으로 탐색하여 모든 항목을 나열하는 프로그램
/// @author <곽형철>
/// @studid <2021-18888>
//
// [전체 프로그램 개요]
// 이 프로그램은 리눅스의 "tree" 명령어와 비슷한 기능을 합니다.
// 주어진 디렉토리부터 시작해서 하위 폴더까지 모두 탐색하면서
// 파일 이름, 소유자, 크기, 블록 수, 타입 등의 정보를 출력합니다.
//
// 주요 기능:
//   1. 디렉토리 트리 재귀 탐색 (하위 폴더 안의 폴더도 계속 탐색)
//   2. 패턴 필터링 (정규식으로 특정 이름만 골라서 보여주기)
//   3. 탐색 깊이 제한 (몇 단계까지만 탐색할지 설정)
//   4. 통계 요약 출력 (파일/폴더/링크 등의 개수와 총 크기)
//--------------------------------------------------------------------------------------------------

// ★ _GNU_SOURCE: GNU 확장 기능(asprintf 등)을 사용하기 위한 매크로 정의
// 이것을 정의하면 표준 C에는 없지만 리눅스에서 제공하는 추가 함수들을 쓸 수 있습니다.
#define _GNU_SOURCE

// ★ 표준 라이브러리 헤더 파일 포함 (include)
// #include는 "이 파일에 있는 함수/타입 선언을 가져와서 쓰겠다"는 의미입니다.
#include <stdio.h>    // printf, fprintf, snprintf 등 입출력 함수
#include <stdlib.h>   // malloc, calloc, free, exit, atoi 등 메모리/프로세스 관련
#include <string.h>   // strcmp, strlen, strdup, memset 등 문자열 처리 함수
#include <stdint.h>   // int8_t 같은 고정 크기 정수 타입
#include <sys/stat.h> // lstat 함수와 struct stat (파일 정보 조회)
#include <sys/types.h>// 다양한 시스템 타입 정의 (uid_t, gid_t 등)
#include <dirent.h>   // opendir, readdir, closedir 등 디렉토리 읽기 함수
#include <errno.h>    // errno 변수 (에러 번호 저장)와 에러 관련 매크로
#include <unistd.h>   // 유닉스 표준 함수 (basename 등)
#include <stdarg.h>   // 가변 인자 함수 (va_list, va_start, va_end)
#include <assert.h>   // assert 매크로 (디버깅용 조건 검사)
#include <grp.h>      // getgrgid (그룹 ID → 그룹 이름 변환)
#include <pwd.h>      // getpwuid (사용자 ID → 사용자 이름 변환)

// ★ 출력 제어 플래그 (비트 플래그 방식)
// 비트 플래그란? 각 비트(0 또는 1)를 하나의 on/off 스위치처럼 사용하는 기법입니다.
// 예: flags = 0x3 이면 이진수로 11 → F_DEPTH(1)도 켜져있고 F_Filter(2)도 켜져 있음
#define F_DEPTH  0x1  // 0x1 = 이진수 01 → 깊이 제한 옵션이 설정되었음을 표시
#define F_Filter 0x2  // 0x2 = 이진수 10 → 패턴 필터링 옵션이 설정되었음을 표시

// ★ 프로그램에서 사용하는 최대값 상수 정의
#define MAX_DIR      64    // 한 번에 처리할 수 있는 최대 디렉토리 개수
#define MAX_PATH_LEN 1024  // 경로 문자열의 최대 길이
#define MAX_DEPTH    20    // 디렉토리 탐색의 최대 깊이 (기본값)

// ★ 실제 사용되는 최대 깊이 값 (전역 변수)
// -d 옵션으로 사용자가 변경할 수 있습니다.
int max_depth = MAX_DEPTH;

// ★ 통계 정보를 저장하는 구조체
// 구조체(struct)란? 여러 개의 변수를 하나로 묶어서 관리하는 "묶음 상자" 같은 것입니다.
// 예: summary라는 상자 안에 dirs, files, links 등 여러 칸이 있는 것.
struct summary
{
  unsigned int dirs;   // 발견한 디렉토리(폴더)의 개수
  unsigned int files;  // 발견한 일반 파일의 개수
  unsigned int links;  // 발견한 심볼릭 링크의 개수 (바로가기와 비슷)
  unsigned int fifos;  // 발견한 파이프(FIFO)의 개수 (프로세스 간 통신용)
  unsigned int socks;  // 발견한 소켓의 개수 (네트워크 통신용)

  unsigned long long size;   // 전체 파일 크기의 합 (바이트 단위)
  unsigned long long blocks; // 전체 블록 수의 합 (512바이트 블록 단위)
};

// ★ 출력에 사용되는 포맷 문자열 배열
// printf의 포맷 문자열을 미리 모아둔 것입니다.
// %-54s: 왼쪽 정렬로 54칸에 문자열 출력
// %10llu: 10칸에 부호 없는 긴 정수 출력
const char *print_formats[8] = {
    "Name                                                        User:Group           Size    Blocks Type\n",  // [0] 헤더
    "----------------------------------------------------------------------------------------------------\n",  // [1] 구분선
    "%-54s  ERROR: %s\n",                          // [2] 에러 출력용
    "%-54s  No such file or directory\n",           // [3] 파일 없음 에러
    "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",  // [4] 파일 정보 출력용 (이름, 유저:그룹, 크기, 블록, 타입)
    "Invalid pattern syntax\n",                     // [5] 잘못된 패턴 문법
    "Out of memory\n",                              // [6] 메모리 부족
};

// ★ 패턴 필터링에 사용되는 패턴 문자열 (전역 변수)
// -f 옵션으로 전달된 패턴이 여기에 저장됩니다.
// NULL이면 필터링 없이 모든 파일을 보여줍니다.
const char *pattern = NULL;

// ★ 프로그램을 에러와 함께 강제 종료하는 함수
// msg: 출력할 에러 메시지, format: 메시지를 어떤 형식으로 출력할지
// exit(EXIT_FAILURE)는 "프로그램이 실패했다"는 의미로 종료합니다.
void panic(const char *msg, const char *format)
{
  if (msg)  // 메시지가 있으면
  {
    if (format)
      fprintf(stderr, format, msg);   // 포맷에 맞춰 에러 스트림에 출력
    else
      fprintf(stderr, "%s\n", msg);   // 그냥 메시지만 출력
  }
  exit(EXIT_FAILURE);  // 프로그램 종료 (실패 코드 반환)
}


// =========================================================================
// [파트 2] 커스텀 미니 정규식 엔진 (AST + 백트래킹 DP)
// =========================================================================
//
// 이 부분은 패턴 매칭(-f 옵션)을 위한 자체 정규식 엔진입니다.
//
// 정규식이란? 문자열에서 특정 패턴을 찾기 위한 "검색 규칙"입니다.
//   예: "*.c" → "아무 글자가 0개 이상 + .c로 끝나는 것"
//   예: "a|b" → "a 또는 b"
//   예: "?" → "아무 글자 1개"
//
// AST(Abstract Syntax Tree, 추상 구문 트리)란?
//   패턴 문자열을 컴퓨터가 이해하기 쉬운 트리 구조로 변환한 것입니다.
//   예: "ab*" → CONCAT(CHAR 'a', STAR(CHAR 'b'))
//       즉, "a 다음에 b가 0개 이상"이라는 트리
//
// DP(Dynamic Programming, 동적 프로그래밍)란?
//   같은 계산을 반복하지 않도록 결과를 저장해두고 재사용하는 기법입니다.
//   이 프로그램에서는 memo 배열에 이전 매칭 결과를 저장합니다.
// =========================================================================

// ★ 정규식 노드의 종류를 나타내는 열거형 (enum)
// enum은 관련된 상수들을 이름 붙여서 묶어놓은 것입니다.
typedef enum
{
  N_CHAR,    // 일반 문자 하나 (예: 'a', 'b', '1')
  N_ANY,     // 아무 문자 1개 (? 에 해당)
  N_CONCAT,  // 연결 (예: "ab" = a 다음에 b)
  N_ALT,     // 선택 (예: "a|b" = a 또는 b)
  N_STAR     // 반복 (예: "a*" = a가 0개 이상)
} NodeType;

// ★ 정규식 AST(추상 구문 트리)의 노드 구조체
// 트리의 각 마디(노드)가 이 구조체 하나입니다.
typedef struct RegexNode
{
  NodeType type;           // 이 노드의 종류 (위의 N_CHAR, N_ANY 등)
  char ch;                 // N_CHAR일 때 실제 문자값 (예: 'a')
  struct RegexNode *left;  // 왼쪽 자식 노드 (트리 구조)
  struct RegexNode *right; // 오른쪽 자식 노드
  int id;                  // DP 메모이제이션용 고유 번호
} RegexNode;

// ★ 전역 변수: 노드 ID 카운터와 파싱 위치
int node_id_counter = 0;   // 새 노드를 만들 때마다 1씩 증가하는 고유 번호
const char *p_curr;        // 패턴 문자열에서 현재 읽고 있는 위치

// ★ 정규식 패턴의 문법이 올바른지 검사하는 함수
// 올바르면 1(참), 잘못되었으면 0(거짓)을 반환합니다.
// 예: "(ab)" → 올바름, "(*)" → 잘못됨, "(()" → 잘못됨
int validate_pattern(const char *p)
{
  if (!p || !*p)       // 패턴이 NULL이거나 빈 문자열이면 잘못된 것
    return 0;
  int len = strlen(p); // 패턴의 길이
  int parens = 0;      // 괄호 짝 맞추기용 카운터

  for (int i = 0; i < len; i++)
  {
    if (p[i] == '(')   // 여는 괄호를 만나면
    {
      parens++;        // 카운터 증가
      // 여는 괄호 바로 뒤에 닫는 괄호, *, | 가 오면 잘못된 문법
      if (i + 1 < len && (p[i + 1] == ')' || p[i + 1] == '*' || p[i + 1] == '|'))
        return 0;
    }
    else if (p[i] == ')')  // 닫는 괄호를 만나면
    {
      parens--;             // 카운터 감소
      // 카운터가 음수면 짝이 안 맞음 / 바로 앞이 |이면 잘못됨
      if (parens < 0 || (i > 0 && p[i - 1] == '|'))
        return 0;
    }
    else if (p[i] == '*')  // 별표(반복)를 만나면
    {
      // 맨 앞이거나, 바로 앞이 (, |, * 이면 잘못됨 (반복할 대상이 없으므로)
      if (i == 0 || p[i - 1] == '(' || p[i - 1] == '|' || p[i - 1] == '*')
        return 0;
    }
    else if (p[i] == '|')  // 파이프(선택)를 만나면
    {
      // 맨 앞/맨 뒤이거나, 바로 앞이 ( 또는 |, 바로 뒤가 ) 이면 잘못됨
      if (i == 0 || i == len - 1 || p[i - 1] == '(' || p[i - 1] == '|' || p[i + 1] == ')')
        return 0;
    }
  }
  return parens == 0;  // 모든 괄호의 짝이 맞으면 올바른 패턴
}

// ★ 함수 전방 선언 (parse_expr을 다른 함수보다 먼저 선언)
// C언어에서는 함수를 사용하기 전에 "이런 함수가 있다"고 미리 알려줘야 합니다.
RegexNode *parse_expr(void);

// ★ 새로운 정규식 AST 노드를 생성하는 함수
// t: 노드 타입, c: 문자(N_CHAR일 때), l: 왼쪽 자식, r: 오른쪽 자식
RegexNode *make_node(NodeType t, char c, RegexNode *l, RegexNode *r)
{
  // calloc: 메모리를 할당하고 0으로 초기화 (malloc + memset(0)과 동일)
  RegexNode *n = calloc(1, sizeof(RegexNode));
  if (!n)  // 메모리 할당 실패 시 프로그램 종료
    panic("Out of memory", print_formats[6]);
  n->type = t;     // 노드 타입 설정
  n->ch = c;       // 문자 설정
  n->left = l;     // 왼쪽 자식 연결
  n->right = r;    // 오른쪽 자식 연결
  n->id = node_id_counter++;  // 고유 ID 부여 후 카운터 증가
  return n;
}

// ★ 패턴의 가장 기본 단위(원자)를 파싱하는 함수
// 원자: 괄호로 묶인 그룹, ? (아무 문자), 일반 문자
RegexNode *parse_atom(void)
{
  if (*p_curr == '(')          // 여는 괄호를 만나면
  {
    p_curr++;                  // 괄호 다음으로 이동
    RegexNode *n = parse_expr(); // 괄호 안의 표현식을 재귀적으로 파싱
    if (*p_curr == ')')        // 닫는 괄호 확인
      p_curr++;                // 닫는 괄호 다음으로 이동
    return n;
  }
  else if (*p_curr == '?')    // ? 를 만나면 (아무 문자 1개)
  {
    p_curr++;
    return make_node(N_ANY, 0, NULL, NULL);  // ANY 노드 생성
  }
  else                         // 일반 문자를 만나면
  {
    char c = *p_curr++;        // 현재 문자를 읽고 다음으로 이동
    return make_node(N_CHAR, c, NULL, NULL); // CHAR 노드 생성
  }
}

// ★ 반복(클레이니 스타, *)을 파싱하는 함수
// 예: "a*" → STAR(CHAR 'a') = "a가 0번 이상 반복"
RegexNode *parse_factor(void)
{
  RegexNode *n = parse_atom();   // 먼저 원자를 파싱
  while (*p_curr == '*')         // 뒤에 *가 있으면 반복 적용
  {
    p_curr++;
    n = make_node(N_STAR, 0, n, NULL);  // 기존 노드를 STAR로 감싸기
  }
  return n;
}

// ★ 연결(concatenation)을 파싱하는 함수
// 예: "abc" → CONCAT(CONCAT(CHAR 'a', CHAR 'b'), CHAR 'c')
// 문자들이 연속으로 나오면 하나씩 읽어서 CONCAT 노드로 연결합니다.
RegexNode *parse_term(void)
{
  RegexNode *n = parse_factor();
  // | 나 ) 이 나오기 전까지, 그리고 문자열 끝이 아닌 동안 계속 연결
  while (*p_curr && *p_curr != '|' && *p_curr != ')')
  {
    RegexNode *right = parse_factor();
    n = make_node(N_CONCAT, 0, n, right);  // 왼쪽과 오른쪽을 연결
  }
  return n;
}

// ★ 선택(alternation, |)을 파싱하는 함수
// 예: "a|b" → ALT(CHAR 'a', CHAR 'b') = "a 또는 b"
RegexNode *parse_expr(void)
{
  RegexNode *n = parse_term();
  while (*p_curr == '|')         // | 를 만나면 선택(OR) 처리
  {
    p_curr++;
    RegexNode *right = parse_term();
    n = make_node(N_ALT, 0, n, right);  // ALT 노드로 묶기
  }
  return n;
}

// ★ 패턴 문자열을 AST(트리)로 컴파일하는 함수
// 입력: 패턴 문자열 (예: "a(b|c)*")
// 출력: AST 루트 노드
RegexNode *compile_regex(const char *pat)
{
  if (!validate_pattern(pat))  // 먼저 문법 검사
    panic("Invalid pattern syntax", print_formats[5]);
  p_curr = pat;                // 파싱 시작 위치 설정
  RegexNode *root = parse_expr();  // 전체 표현식 파싱
  if (*p_curr != '\0')         // 파싱 후 남은 문자가 있으면 에러
    panic("Invalid pattern syntax", print_formats[5]);
  return root;
}

// ★ AST 메모리를 재귀적으로 해제하는 함수
// 트리의 모든 노드를 방문하면서 메모리를 반환합니다.
void free_regex(RegexNode *n)
{
  if (!n) return;            // NULL이면 아무것도 안 함
  free_regex(n->left);      // 왼쪽 서브트리 먼저 해제
  free_regex(n->right);     // 오른쪽 서브트리 해제
  free(n);                  // 현재 노드 메모리 해제
}

// ★ DP(메모이제이션) 기반 매칭 엔진
// 문자열 s의 [i, j) 구간이 노드 n에 매칭되는지 검사합니다.
// memo: 이전 결과를 저장하는 배열 (-1=미계산, 0=불일치, 1=일치)
int memo_match(RegexNode *n, const char *s, int i, int j, int8_t *memo, int num_nodes, int len)
{
  // ★ 3차원 인덱스를 1차원으로 변환하여 memo 배열에 접근
  // (노드 ID, 시작 위치 i, 끝 위치 j)의 조합으로 고유한 위치를 계산
  int idx = n->id * ((len + 1) * (len + 1)) + i * (len + 1) + j;

  if (memo[idx] != -1)     // 이미 계산한 적이 있으면 저장된 결과 반환
    return memo[idx];

  int res = 0;  // 결과값 (0=불일치)

  if (n->type == N_CHAR)
    // 문자 노드: 구간 길이가 정확히 1이고, 그 문자가 일치해야 함
    res = (j - i == 1) && (s[i] == n->ch);
  else if (n->type == N_ANY)
    // ANY 노드: 구간 길이가 정확히 1이면 됨 (어떤 문자든 OK)
    res = (j - i == 1);
  else if (n->type == N_CONCAT)
  {
    // 연결 노드: 구간을 두 부분으로 나눠서 왼쪽/오른쪽이 각각 매칭되는지 확인
    for (int k = i; k <= j; k++)
    {
      if (memo_match(n->left, s, i, k, memo, num_nodes, len) &&
          memo_match(n->right, s, k, j, memo, num_nodes, len))
      {
        res = 1;  // 나눌 수 있는 지점을 찾으면 매칭 성공
        break;
      }
    }
  }
  else if (n->type == N_ALT)
  {
    // 선택 노드: 왼쪽 또는 오른쪽 중 하나라도 매칭되면 성공
    res = memo_match(n->left, s, i, j, memo, num_nodes, len) ||
          memo_match(n->right, s, i, j, memo, num_nodes, len);
  }
  else if (n->type == N_STAR)
  {
    // 반복 노드: 빈 문자열도 매칭됨 (0번 반복)
    if (i == j)
      res = 1;
    else
    {
      // 첫 부분이 자식과 매칭되고, 나머지가 다시 STAR와 매칭되는 지점 찾기
      for (int k = i + 1; k <= j; k++)
      {
        if (memo_match(n->left, s, i, k, memo, num_nodes, len) &&
            memo_match(n, s, k, j, memo, num_nodes, len))
        {
          res = 1;
          break;
        }
      }
    }
  }
  return memo[idx] = res;  // 결과를 저장하고 반환
}

// ★ 패턴 매칭의 진입점 함수
// 문자열 s의 어떤 부분 문자열이라도 패턴과 매칭되는지 검사합니다.
// (전체가 아니라 일부분만 매칭돼도 OK)
int check_match(RegexNode *root, const char *s)
{
  if (!root) return 1;      // 패턴이 없으면 모든 것이 매칭됨

  int len = strlen(s);
  // memo 배열 할당: 노드 수 × (길이+1) × (길이+1) 크기
  int8_t *memo = malloc(node_id_counter * (len + 1) * (len + 1) * sizeof(int8_t));
  memset(memo, -1, node_id_counter * (len + 1) * (len + 1) * sizeof(int8_t));

  int matched = 0;
  // 모든 가능한 시작점(i)과 끝점(j)에 대해 매칭 시도
  for (int i = 0; i <= len && !matched; i++)
  {
    for (int j = i; j <= len && !matched; j++)
    {
      if (memo_match(root, s, i, j, memo, node_id_counter, len))
        matched = 1;  // 부분 문자열 매칭 발견!
    }
  }
  free(memo);    // memo 배열 메모리 해제
  return matched;
}


// =========================================================================
// [파트 3] 디렉토리 트리 탐색 로직
// =========================================================================
//
// 이 부분이 프로그램의 핵심입니다.
// 1. build_tree: 디렉토리를 재귀적으로 읽어서 메모리에 트리 구조를 만듦
// 2. print_tree: 만든 트리를 화면에 출력하면서 통계도 계산
// 3. process_dir: 위 두 함수를 순서대로 호출
// =========================================================================

// ★ 컴파일된 정규식 AST의 루트 노드 (전역 변수)
// -f 옵션이 없으면 NULL이고, 있으면 패턴을 파싱한 트리가 저장됩니다.
RegexNode *regex_root = NULL;

// ★ 디렉토리 트리의 노드 구조체
// 파일이나 폴더 하나를 나타냅니다. 폴더인 경우 자식 노드들을 가집니다.
typedef struct TreeNode
{
  char *name;            // 파일/폴더의 이름 (예: "hello.c")
  struct stat st;        // 파일 상태 정보 (크기, 소유자, 권한 등)
  int is_dir;            // 디렉토리(폴더)인지 여부 (1=폴더, 0=아님)
  int is_symlink;        // 심볼릭 링크인지 여부 (바로가기와 비슷)
  int is_fifo;           // FIFO(파이프)인지 여부
  int is_socket;         // 소켓인지 여부
  int has_error;         // 이 파일/폴더를 읽는 중 에러가 발생했는지
  char *error_msg;       // 에러 메시지 문자열

  struct TreeNode **children;  // 자식 노드 배열 (폴더인 경우)
  int num_children;            // 자식 노드의 개수

  int matches;           // 이 노드의 이름이 패턴과 매칭되는지
  int subtree_matches;   // 이 노드의 서브트리(하위) 중 매칭되는 것이 있는지
  int is_placeholder;    // 자기 자신은 매칭 안 되지만, 하위에 매칭되는 것이 있어서 표시만 하는 경우
} TreeNode;

// ★ 디렉토리에서 다음 항목을 읽는 함수
// "." (현재 디렉토리)과 ".." (상위 디렉토리)는 무시합니다.
// 왜냐하면 이것들을 포함하면 무한 루프가 되기 때문입니다.
struct dirent *get_next(DIR *dir)
{
  struct dirent *next;
  int ignore;
  do
  {
    errno = 0;                    // 에러 번호 초기화
    next = readdir(dir);          // 디렉토리에서 다음 항목 읽기
    if (errno != 0)               // 읽기 중 에러가 발생하면
      perror(NULL);               // 에러 메시지 출력
    // "." 또는 ".."인지 확인
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);      // 무시할 항목이면 다시 읽기
  return next;                    // 유효한 항목 또는 NULL(더 이상 없음) 반환
}

// ★ 정렬 비교 함수 (qsort에서 사용)
// 규칙 1: 디렉토리(폴더)가 파일보다 먼저 옴
// 규칙 2: 같은 종류끼리는 이름의 알파벳 순서로 정렬
static int dirent_compare(const void *a, const void *b)
{
  TreeNode *n1 = *(TreeNode **)a;
  TreeNode *n2 = *(TreeNode **)b;
  if (n1->is_dir != n2->is_dir)    // 하나는 폴더, 하나는 파일이면
    return n1->is_dir ? -1 : 1;    // 폴더가 앞으로 (음수 = a가 앞)
  return strcmp(n1->name, n2->name); // 이름으로 비교
}

// ★ 경로에서 파일 이름만 추출하는 함수
// 예: "/home/user/test.c" → "test.c"
const char *get_basename(const char *path)
{
  const char *base = strrchr(path, '/'); // 마지막 '/' 위치 찾기
  return base ? base + 1 : path;         // '/'가 있으면 그 다음부터, 없으면 전체가 이름
}

// ★ TreeNode 메모리를 재귀적으로 해제하는 함수
// 트리의 모든 노드를 순회하면서 할당했던 메모리를 반납합니다.
void free_tree(TreeNode *n)
{
  if (!n) return;
  for (int i = 0; i < n->num_children; i++)
    free_tree(n->children[i]);   // 자식들 먼저 해제 (재귀)
  free(n->children);             // 자식 배열 해제
  free(n->name);                 // 이름 문자열 해제
  if (n->error_msg)
    free(n->error_msg);          // 에러 메시지 해제
  free(n);                       // 노드 자체 해제
}

// ★ 디렉토리 트리를 재귀적으로 구축하는 핵심 함수
// path: 파일/폴더의 전체 경로, name: 이름만, depth: 현재 깊이
// 반환값: 생성된 TreeNode (필터링에 의해 제외되면 NULL)
TreeNode *build_tree(const char *path, const char *name, int depth)
{
  // 1단계: 노드 생성 및 초기화
  TreeNode *node = calloc(1, sizeof(TreeNode)); // 0으로 초기화된 메모리 할당
  node->name = strdup(name);                    // 이름 문자열 복사

  // 2단계: lstat로 파일 정보 가져오기
  // lstat: 파일의 상태 정보(크기, 소유자, 타입 등)를 가져오는 시스템 콜
  // 심볼릭 링크의 경우 링크 자체의 정보를 가져옵니다 (stat은 링크가 가리키는 대상의 정보)
  if (lstat(path, &node->st) < 0)
  {
    // lstat 실패 시 (예: 권한 없음) 에러 정보 저장
    node->has_error = 1;
    node->error_msg = strdup(strerror(errno)); // errno를 사람이 읽을 수 있는 메시지로 변환
    node->matches = (regex_root == NULL) || check_match(regex_root, name);
    node->subtree_matches = node->matches;
    return node;
  }

  // 3단계: 파일 타입 판별 (S_ISDIR 등은 매크로로 st_mode 비트를 검사)
  node->is_dir    = S_ISDIR(node->st.st_mode);   // 디렉토리인지?
  node->is_symlink = S_ISLNK(node->st.st_mode);  // 심볼릭 링크인지?
  node->is_fifo   = S_ISFIFO(node->st.st_mode);  // FIFO인지?
  node->is_socket = S_ISSOCK(node->st.st_mode);   // 소켓인지?

  // 4단계: 패턴 매칭 검사
  // regex_root가 NULL이면 (패턴 없음) 항상 매칭, 아니면 check_match로 검사
  node->matches = (regex_root == NULL) || check_match(regex_root, name);
  node->subtree_matches = node->matches;

  // 5단계: 디렉토리인 경우 하위 항목들을 재귀적으로 처리
  if (node->is_dir && depth < max_depth)
  {
    DIR *dir = opendir(path);  // 디렉토리 열기
    if (!dir)
    {
      // 디렉토리를 열 수 없으면 에러 기록
      node->has_error = 1;
      node->error_msg = strdup(strerror(errno));
    }
    else
    {
      struct dirent *ent;
      int cap = 16;  // 자식 배열의 초기 용량
      node->children = malloc(cap * sizeof(TreeNode *));

      // 디렉토리의 모든 항목을 하나씩 읽기
      while ((ent = get_next(dir)) != NULL)
      {
        // 자식의 전체 경로 만들기 (예: "/home" + "/" + "user" = "/home/user")
        char *child_path;
        if (asprintf(&child_path, "%s/%s", strcmp(path, "/") == 0 ? "" : path, ent->d_name) < 0)
          panic("Out of memory", print_formats[6]);

        // 자식 노드를 재귀적으로 생성 (여기서 다시 build_tree 호출!)
        TreeNode *child = build_tree(child_path, ent->d_name, depth + 1);
        free(child_path);  // 경로 문자열 메모리 해제

        if (child)  // NULL이 아니면 (필터링에 의해 제외되지 않았으면)
        {
          // 배열 용량이 부족하면 2배로 늘리기 (동적 배열)
          if (node->num_children >= cap)
          {
            cap *= 2;
            node->children = realloc(node->children, cap * sizeof(TreeNode *));
          }
          node->children[node->num_children++] = child;  // 자식 추가
        }
      }
      closedir(dir);  // 디렉토리 닫기

      // 자식들을 정렬 (폴더 먼저, 그 다음 알파벳 순)
      qsort(node->children, node->num_children, sizeof(TreeNode *), dirent_compare);

      // 서브트리에 매칭되는 것이 있는지 확인
      for (int i = 0; i < node->num_children; i++)
      {
        if (node->children[i]->subtree_matches)
        {
          node->subtree_matches = 1;  // 하위에 매칭되는 것이 있음!
          break;
        }
      }
    }
  }

  // 6단계: 필터링 규칙 적용
  // 일반 파일인데 패턴에 매칭되지 않으면 → 제외 (NULL 반환)
  if (!node->is_dir && !node->matches)
  {
    free_tree(node);
    return NULL;
  }
  // 디렉토리인데 서브트리에도 매칭되는 것이 없으면 → 제외
  if (node->is_dir && !node->subtree_matches)
  {
    free_tree(node);
    return NULL;
  }
  // 디렉토리 자체는 매칭 안 되지만 하위에 매칭되는 것이 있으면 → 플레이스홀더
  // (이름만 표시하고 상세 정보는 생략)
  if (node->is_dir && !node->matches && node->subtree_matches)
  {
    node->is_placeholder = 1;
  }

  return node;
}

// ★ 트리를 순회하면서 화면에 출력하고 통계를 계산하는 함수
// depth: 현재 깊이 (들여쓰기에 사용), stats: 통계 누적용 구조체
void print_tree(TreeNode *node, int depth, struct summary *stats)
{
  if (!node) return;

  // 이름 문자열 준비 (들여쓰기 + 이름, 너무 길면 "..."으로 축약)
  char name_buf[256];
  int indent = depth * 2;           // 깊이에 따른 들여쓰기 칸 수
  int max_name_len = 54 - indent;   // 남은 칸 수

  if ((int)strlen(node->name) > max_name_len)
  {
    // 이름이 너무 길면 잘라서 "..." 붙이기
    snprintf(name_buf, sizeof(name_buf), "%*s%.*s...", indent, "", max_name_len - 3, node->name);
  }
  else
  {
    // 정상적으로 들여쓰기 + 이름
    snprintf(name_buf, sizeof(name_buf), "%*s%s", indent, "", node->name);
  }

  if (node->is_placeholder)
  {
    // 플레이스홀더: 이름만 출력 (상세 정보 없음)
    printf("%s\n", name_buf);
  }
  else if (node->has_error && node->error_msg)
  {
    // 에러가 있는 경우: 이름 + 에러 메시지 출력
    printf("%s\n", name_buf);
    printf("%*sERROR: %s\n", indent + 2, "", node->error_msg);
  }
  else if (depth == 0)
  {
    // 루트(최상위) 디렉토리: 이름만 출력
    printf("%s\n", name_buf);
  }
  else
  {
    // 일반 항목: 이름, 소유자, 그룹, 크기, 블록 수, 타입 출력

    // 소유자(user) 이름 가져오기
    char user[32] = {0};
    char group[32] = {0};
    struct passwd *pw = getpwuid(node->st.st_uid);  // UID → 사용자 정보
    if (pw)
      strncpy(user, pw->pw_name, 8);    // 사용자 이름 복사 (최대 8글자)
    else
      snprintf(user, sizeof(user), "%d", node->st.st_uid); // 이름 없으면 숫자로

    // 그룹(group) 이름 가져오기
    struct group *gr = getgrgid(node->st.st_gid);   // GID → 그룹 정보
    if (gr)
      strncpy(group, gr->gr_name, 8);   // 그룹 이름 복사
    else
      snprintf(group, sizeof(group), "%d", node->st.st_gid);

    // 파일 타입 문자 결정
    char type_c = ' ';           // 기본값: 일반 파일 (공백)
    if (node->is_dir)       type_c = 'd';  // 디렉토리
    else if (node->is_symlink) type_c = 'l';  // 링크
    else if (node->is_fifo)    type_c = 'f';  // 파이프
    else if (node->is_socket)  type_c = 's';  // 소켓

    // 정보 출력 (포맷: 이름  사용자:그룹  크기  블록수  타입)
    printf(print_formats[4], name_buf, user, group,
           (unsigned long long)node->st.st_size, (unsigned long long)node->st.st_blocks, type_c);

    // 통계 누적 (루트 디렉토리 자체는 제외)
    if (depth > 0)
    {
      if (node->is_dir)          stats->dirs++;
      else if (node->is_symlink) stats->links++;
      else if (node->is_fifo)    stats->fifos++;
      else if (node->is_socket)  stats->socks++;
      else                       stats->files++;

      stats->size   += node->st.st_size;    // 총 크기 누적
      stats->blocks += node->st.st_blocks;  // 총 블록 수 누적
    }
  }

  // 자식 노드들을 재귀적으로 출력 (깊이 + 1)
  for (int i = 0; i < node->num_children; i++)
  {
    print_tree(node->children[i], depth + 1, stats);
  }
}

// ★ 통계 요약을 포맷에 맞게 출력하는 함수
// 예: "3 files, 2 directories, 0 links, 0 pipes, and 0 sockets    12345   24"
void print_summary(struct summary *stats)
{
  char buf[256];
  // 단수/복수 처리 (1개면 "file", 2개 이상이면 "files")
  // directory는 특이하게 1개="directory", 2개이상="directories"
  snprintf(buf, sizeof(buf), "%u file%s, %u director%s, %u link%s, %u pipe%s, and %u socket%s",
           stats->files, stats->files == 1 ? "" : "s",
           stats->dirs,  stats->dirs  == 1 ? "y" : "ies",
           stats->links, stats->links == 1 ? "" : "s",
           stats->fifos, stats->fifos == 1 ? "" : "s",
           stats->socks, stats->socks == 1 ? "" : "s");

  // 너무 길면 "..."으로 축약
  if (strlen(buf) > 68)
    strcpy(buf + 65, "...");

  printf("%-68s %14llu %9llu\n", buf, stats->size, stats->blocks);
}

// ★ 프로그램 사용법을 출력하고 종료하는 함수
// 잘못된 옵션을 입력했을 때 호출됩니다.
// "..."는 가변 인자 (printf처럼 여러 개의 인자를 받을 수 있음)
void syntax(const char *argv0, const char *error, ...)
{
  if (error)
  {
    va_list ap;              // 가변 인자 리스트
    va_start(ap, error);     // 가변 인자 시작
    vfprintf(stderr, error, ap);  // 에러 메시지 출력
    va_end(ap);              // 가변 인자 끝
    printf("\n\n");
  }

  assert(argv0 != NULL);    // argv0이 NULL이면 프로그램 오류 (디버깅용)

  // 사용법 안내 (어떤 옵션들이 있는지 설명)
  fprintf(stderr, "Usage %s [-d depth] [-f pattern] [-h] [path...]\n"
                  "Recursively traverse directory tree and list all entries. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -d depth   | set maximum depth of directory traversal (1-%d)\n"
                  " -f pattern | filter entries using pattern (supports '?', '*' and '()')\n"
                  " -h         | print this help\n"
                  " path...    | list of space-separated paths (max %d). Default is the current directory.\n",
          basename(argv0), MAX_DEPTH, MAX_DIR);
  exit(EXIT_FAILURE);
}

// ★ 하나의 디렉토리를 처리하는 함수
// 트리를 만들고(build_tree), 출력하고(print_tree), 메모리를 해제합니다(free_tree).
void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  TreeNode *root = build_tree(dn, dn, 0);  // 트리 구축 (깊이 0부터 시작)
  if (root)
  {
    print_tree(root, 0, stats);   // 트리 출력 및 통계 계산
    free_tree(root);              // 트리 메모리 해제
  }
}


// =========================================================================
// [파트 4] main 함수 (프로그램 시작점)
// =========================================================================
//
// 프로그램이 실행되면 가장 먼저 이 함수가 호출됩니다.
// argc: 명령줄 인자의 개수 (프로그램 이름 포함)
// argv: 명령줄 인자 문자열 배열
//
// 사용 예시:
//   ./dirtree                    → 현재 디렉토리 탐색
//   ./dirtree /home /tmp         → /home과 /tmp 탐색
//   ./dirtree -d 3               → 깊이 3까지만 탐색
//   ./dirtree -f "*.c"           → .c 파일만 필터링
// =========================================================================
int main(int argc, char *argv[])
{
  const char CURDIR[] = ".";          // 기본 경로: 현재 디렉토리
  const char *directories[MAX_DIR];   // 탐색할 디렉토리 경로 배열
  int ndir = 0;                       // 실제 디렉토리 개수

  struct summary tstat = {0};         // 전체 통계 (여러 디렉토리의 합산)
  unsigned int flags = 0;             // 옵션 플래그

  // ★ 명령줄 인자 파싱 (하나씩 읽으면서 처리)
  for (int i = 1; i < argc; i++)      // i=0은 프로그램 이름이므로 1부터
  {
    if (argv[i][0] == '-')            // '-'로 시작하면 옵션
    {
      if (!strcmp(argv[i], "-d"))      // "-d" 옵션: 깊이 제한
      {
        flags |= F_DEPTH;             // 깊이 플래그 켜기
        if (++i < argc && argv[i][0] != '-')  // 다음 인자가 있고 숫자이면
        {
          max_depth = atoi(argv[i]);   // 문자열을 정수로 변환
          if (max_depth < 1 || max_depth > MAX_DEPTH)  // 범위 검사
          {
            syntax(argv[0], "Invalid depth value '%s'. Must be between 1 and %d.", argv[i], MAX_DEPTH);
          }
        }
        else
          syntax(argv[0], "Missing depth value argument.");
      }
      else if (!strcmp(argv[i], "-f"))  // "-f" 옵션: 패턴 필터
      {
        if (++i < argc && argv[i][0] != '-')
        {
          flags |= F_Filter;
          pattern = argv[i];
          regex_root = compile_regex(pattern);  // 패턴을 AST로 컴파일
        }
        else
          syntax(argv[0], "Missing filtering pattern argument.");
      }
      else if (!strcmp(argv[i], "-h"))  // "-h" 옵션: 도움말
        syntax(argv[0], NULL);
      else                              // 알 수 없는 옵션
        syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    }
    else
    {
      // 옵션이 아닌 인자 → 탐색할 디렉토리 경로
      if (ndir < MAX_DIR)
        directories[ndir++] = argv[i];
      else
        fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
    }
  }

  // 디렉토리가 하나도 지정되지 않으면 현재 디렉토리를 기본으로 사용
  if (ndir == 0)
    directories[ndir++] = CURDIR;

  // ★ 각 디렉토리를 순서대로 처리
  for (int i = 0; i < ndir; i++)
  {
    if (i > 0)
      printf("\n");   // 디렉토리 사이에 빈 줄 삽입

    struct summary dstat = {0};  // 이 디렉토리의 개별 통계

    // 헤더와 구분선 출력
    printf("%s", print_formats[0]);  // "Name  User:Group  Size  Blocks Type"
    printf("%s", print_formats[1]);  // "----...----"

    // 디렉토리 처리 (트리 구축 → 출력 → 해제)
    process_dir(directories[i], "", &dstat, flags);

    // 구분선과 요약 통계 출력
    printf("%s", print_formats[1]);
    print_summary(&dstat);

    // 전체 통계에 이 디렉토리의 통계를 합산
    tstat.dirs   += dstat.dirs;
    tstat.files  += dstat.files;
    tstat.links  += dstat.links;
    tstat.fifos  += dstat.fifos;
    tstat.socks  += dstat.socks;
    tstat.size   += dstat.size;
    tstat.blocks += dstat.blocks;
  }

  // ★ 여러 디렉토리를 처리한 경우 전체 통계 출력
  if (ndir > 1)
  {
    printf("\nAnalyzed %d directories:\n"
           "  total # of files:        %16d\n"
           "  total # of directories:  %16d\n"
           "  total # of links:        %16d\n"
           "  total # of pipes:        %16d\n"
           "  total # of sockets:      %16d\n"
           "  total # of entries:      %16d\n"
           "  total file size:         %16llu\n"
           "  total # of blocks:       %16llu\n",
           ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks,
           tstat.files + tstat.dirs + tstat.links + tstat.fifos + tstat.socks,
           tstat.size, tstat.blocks);
  }

  // ★ 정규식 AST 메모리 해제 (프로그램 종료 전 정리)
  if (regex_root)
    free_regex(regex_root);

  return EXIT_SUCCESS;  // 프로그램 정상 종료 (종료 코드 0)
}

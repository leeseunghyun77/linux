#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("사용법: %s [숫자1] [연산자] [숫자2]\n", argv[0]);
        return 1;
    }

    double num1 = atof(argv[1]);
    double num2 = atof(argv[3]);
    char *op = argv[2];
    double result;

    if (strcmp(op, "+") == 0) {
        result = num1 + num2;
    } else if (strcmp(op, "-") == 0) {
        result = num1 - num2;
    } else if (strcmp(op, "*") == 0) {
        result = num1 * num2;
    } else if (strcmp(op, "/") == 0) {
        if (num2 == 0) {
            printf("오류: 0으로 나눌 수 없습니다.\n");
            return 1;
        }
        result = num1 / num2;
    } else {
        printf("오류: 지원하지 않는 연산자입니다. (+, -, *, /)\n");
        return 1;
    }

    printf("결과: %.2f\n", result);
    return 0;
}

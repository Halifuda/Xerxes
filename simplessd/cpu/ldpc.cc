// This code is only used for counting the CPU overhead of LDPC

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define N 4096   // 矩阵的列数，即码长
#define M 2048    // 矩阵的行数
#define WC 3   // 每列中的1的数量
#define WR 6   // 每行中的1的数量
#define K (N-M) // 信息位数
#define MAX_ITER 50 // 最大迭代次数

void generateLDPC(int H[M][N]) {
    int row_count[M] = {0};
    int col_count[N] = {0};
    int i, j, rnd;

    srand(time(NULL)); // 初始化随机数生成器

    // 初始化矩阵
    for (i = 0; i < M; i++) {
        for (j = 0; j < N; j++) {
            H[i][j] = 0;
        }
    }

    // 填充矩阵
    for (i = 0; i < M; i++) {
        for (j = 0; j < WR; j++) {
            do {
                rnd = rand() % N;
            } while (col_count[rnd] >= WC || H[i][rnd] == 1);
            H[i][rnd] = 1;
            col_count[rnd]++;
        }
    }
}

void encode(int *input, int *output, int H[M][N]) {
    for (int i = 0; i < K; i++) {
        output[i] = input[i];
    }

    for (int j = K; j < N; j++) {
        output[j] = 0;
        for (int i = 0; i < K; i++) {
            if (H[j-K][i] == 1) {
                output[j] ^= input[i];
            }
        }
    }
}

void decode(int *received, int *decoded, int H[M][N]) {
    double p1[N], p0[N], r[M][N];
    int i, j, k, iter, l;

    // 初始化概率
    for (j = 0; j < N; j++) {
        p1[j] = received[j] == 1 ? 0.75 : 0.25; // 简化的信道模型
        p0[j] = 1 - p1[j];
    }

    // 置信传播算法
    for (iter = 0; iter < MAX_ITER; iter++) {
        // 消息从变量节点到校验节点
        for (i = 0; i < M; i++) {
            for (j = 0; j < N; j++) {
                if (H[i][j]) {
                    double product = 1.0;
                    for (k = 0; k < N; k++) {
                        if (k != j && H[i][k]) {
                            product *= (2 * p1[k] - 1);
                        }
                    }
                    r[i][j] = (1 + product) / 2;
                }
            }
        }

        // 消息从校验节点回变量节点
        for (j = 0; j < N; j++) {
            double q1 = p1[j], q0 = p0[j];
            for (i = 0; i < M; i++) {
                if (H[i][j]) {
                    double temp = 2 * r[i][j] - 1;
                    q1 *= temp;
                    q0 *= -temp;
                }
            }
            p1[j] = q1 / (q1 + q0);
            p0[j] = 1 - p1[j];
        }
    }

    // 决策
    for (j = 0; j < K; j++) {
        decoded[j] = p1[j] > 0.5 ? 1 : 0;
    }
}

int main() {
    int H[M][N]; // 校验矩阵
    int input[K] = {1, 0, 1, 0, 1, 0}; // 示例输入信息位
    int encoded[N]; // 编码后的数据
    int decoded[K]; // 解码后的数据

    generateLDPC(H);
    encode(input, encoded, H);
    decode(encoded, decoded, H);

    printf("Encoded data: ");
    for (int i = 0; i < N; i++) {
        printf("%d ", encoded[i]);
    }
    printf("\n");

    printf("Decoded data: ");
    for (int i = 0; i < K; i++) {
        printf("%d ", decoded[i]);
    }
    printf("\n");

    return 0;
}
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define N 4

// 사인값 계산
void sinx_taylor_child(int terms, double x, int write_fd){
	double value = x;
	double numer = x * x * x;
	double denom = 6;
	int sign = -1;

	for (int j = 1; j <= terms; j++) {
	        value += (double)sign * numer / denom;
        	numer *= x * x;
        	denom *= (2. * (double)j + 2) * (2. * (double)j + 3);
        	sign *= -1;
    	}

	write(write_fd, &value, sizeof(double));
	close(write_fd);
	exit(0);

}

void sinx_taylor(int num_elements, int terms, double* x, double* result)
{
	int pipes[num_elements][2];	    // 자식마다 파이프

	for (int i=0;i<num_elements; i++){
		if (pipe(pipes[i]) == -1){
			perror("pipe");
			exit(1);
		}	
		
		pid_t pid = fork();
		if (pid < 0){
			perror("fork");
			exit(1);
		}
		if (pid == 0) {		    // 자식 프로세스
            		close(pipes[i][0]); // 읽기 닫기
            		sinx_taylor_child(terms, x[i], pipes[i][1]);
        	} else {
            		close(pipes[i][1]); // 부모는 쓰기 닫기
		}
	}

	for (int i=0; i<num_elements; i++){
		read(pipes[i][0], &result[i], sizeof(double));
		close(pipes[i][0]);
		wait(NULL);
	}
}




int main()
{
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];

	sinx_taylor(N, 3, x, res);
	for(int i=0; i<N; i++){
		printf("sin(%.2f) by Taylor seriex = %f\n", x[i], res[i]);
		printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
	}
	return 0;
}

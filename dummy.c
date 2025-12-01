#include <stdio.h>

int main(){
    unsigned char argc;
    size_t n = fread(&argc, 1, 1, stdin);
    if (n == 0) {
        return 4;
    }
    argc -= '0';
    printf("Read from stdin: %d\n", (int)(argc));
    int ret = 0;
    if(argc == 1){
        ret = 1;
    }else if (argc == 2) {
        ret = 2;
    }else{
        ret = 3;
    }
    return ret;
}
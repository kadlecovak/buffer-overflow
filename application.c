#include <stdio.h>

int main (){
    char name[48];
    printf("Enter your name: ");
    gets(name);
    printf("Hello, %s", name);
    return 0;
}
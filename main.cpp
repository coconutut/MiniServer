#include <MiniServer.h>

int main(){

    MiniServer ms(7896, 4);

    ms.run();
    
    return 0;
}
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h" // 必须包含，因为我们在内核里定义了 PROT_ 和 MAP_ 宏

void _assert(int condition, const char* msg) {
    if (!condition) {
        printf("TEST FAILED: %s\n", msg);
        exit(1);
    }
}

void test_basic_mmap() {
    printf("test_basic_mmap...\n");
    int fd = open("test_basic", O_RDWR | O_CREATE);
    _assert(fd >= 0, "Open failed");
    
    // 写入初始数据
    write(fd, "hello world", 11);
    
    // MAP_SHARED 映射
    // 注意：mmap 返回 void*，在 C 中可以直接赋值给 char*，无需强转
    char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    _assert(p != (char*)-1, "mmap failed");
    
    // 验证读取 (触发缺页中断)
    // [修复] 比较 p[0] 而不是 p
    if(p[0] != 'h') _assert(0, "Read data mismatch");
    
    // 验证写入
    // [修复] 修改 p[0] 而不是 p
    p[0] = 'H';
    
    // 解除映射 (应触发脏页回写)
    munmap(p, 4096);
    close(fd);
    
    // 验证持久化
    fd = open("test_basic", O_RDONLY);
    // [修复] buf 大小必须足够容纳读取的数据 (11字节)
    char buf[16]; 
    read(fd, buf, 11);
    // [修复] 比较 buf[0] 而不是 buf 地址
    _assert(buf[0] == 'H', "Persistence failed: Dirty page not written back");
    close(fd);
    printf("test_basic_mmap: OK\n");
}

void test_private_mapping() {
    printf("test_private_mapping...\n");
    int fd = open("test_private", O_RDWR | O_CREATE);
    write(fd, "secret data", 11);
    
    // MAP_PRIVATE 映射
    char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    _assert(p != (char*)-1, "mmap private failed");
    
    // 修改内存
    // [修复] 使用 p[0]
    p[0] = 'S'; // 原本是 's'
    
    // 解除映射 (私有映射不应写回磁盘)
    munmap(p, 4096);
    close(fd);
    
    // 验证文件内容未改变
    fd = open("test_private", O_RDONLY);
    char buf[16];
    read(fd, buf, 11);
    // [修复] 验证文件内容仍为 's'
    _assert(buf[0] == 's', "Privacy violation: Private mapping wrote to file!");
    close(fd);
    printf("test_private_mapping: OK\n");
}

void test_vma_exhaustion() {
    printf("test_vma_exhaustion...\n");
    int fd = open("test_oom", O_RDWR | O_CREATE);
    
    // 假设内核硬编码限制了 VMA 数量 (例如 16 个)
    // 我们尝试映射 32 次，预期后面会失败
    int success_count = 0;
    for(int i = 0; i < 32; i++) {
        char *p = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, 0);
        if(p != (char*)-1) {
            success_count++;
        }
    }
    
    printf("Managed to create %d mappings\n", success_count);
    // 这里的阈值取决于你的内核实现，通常 xv6 实验设置为 16
    _assert(success_count >= 16, "Should support at least 16 mappings"); 
    _assert(success_count < 32, "Should have failed eventually (VMA slots exhaustion)");
    
    close(fd);
    printf("test_vma_exhaustion: OK\n");
}

int main(int argc, char *argv[]) {
    test_basic_mmap();
    test_private_mapping();
    test_vma_exhaustion();
    printf("ALL TESTS PASSED\n");
    exit(0);
}
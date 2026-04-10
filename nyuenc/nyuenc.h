#include<iostream>
#include<fcntl.h>//open(char* pathname, int flags(O_RDONLY,O_WONLY,etc)),
#include <sys/stat.h>// int fstat(int fd,struct stat*buf). then, we can use: buf.st_size,buf.st_mode,etc
#include <unistd.h> // int close(int fd)。 success:return 0 failure: return -1
#include <sys/mman.h>//void*ptr=mmap(void*addr,size_t length,int prot,int flags,int fd, off_t offset)
#include <cstdint>//使用uint8_t type
#include <thread>
#include<mutex>
#include<condition_variable>
#include<memory>//如，std::shared_ptr,std::unique_ptr 等
#include<atomic>
#define max_task_size((4UL)*(1024UL))//一个task最多4KB

namespace nyuenc{
     class Pair{
        public:
        uint8_t ch;
        uint8_t cnt;
        Pair(uint8_t ch,uint8_t cnt):ch{ch},cnt{cnt}{}
    }

     class Task{
        public:
        int task_id;
        size_t start;//代表在文件里的字节位置，有可能很大很大
        size_t num_bytes;
        uint8_t* data;
        std::vector<Pair> pairs{};
        Task(int tid):task_id(tid){}
    }

     class nyuenc{
        public:
        int task_count;
        uint8_t pending_char=0;
        size_t pending_cnt=0;
        int has_pending=0;

        void send_pair(uint8_t ch,uint8_t cnt){
            std::cout.put(static_cast<char>(ch));
            std::cout.put(static_cast<char>(cnt));
        }
        
        void append_pair(Task* task,uint8_t ch,size_t cnt){//append or externd a pair at a specific task
            if( !(task->pairs.empty()) && task->pairs.back().ch==ch){
                task->pairs.back().cnt+=cnt;
                return;
            }
            // if(task->pairs.back().ch==ch){
            //     task->pairs.back().cnt+=cnt;
            //     return;
            // }
            task->pairs.push_back(Pair(ch,cnt));
        }

        void encode_task(Task* task){
            //append_pair is helper method here, encode all data of this task to pairs.
            if (task->num_bytes==0){
                return;
            }
            int start=task->start;
            size_t len=task->num_bytes;
            uint8_t start_ch=task->data[start];
            size_t cnt=1;
            for(size_t i=1;i<len;++i){
                if (task->data[start+i]==start_ch){
                    cnt+=1;
                    continue;
                }
                append_pair(task,start_ch,cnt);
                cnt=1;
                start_ch=task->data[start+i];
            }
            append_pair(task,start_ch,cnt);
        }
 void submit_task_to_stdout(Task*task){
            if(task->pairs.empty()){
                if(has_pending && task->task_id==task_count-1){
                    send_pair(pending_ch,pending_cnt);
                std::cout.flush();
                }
                return;
            }
            for(size_t i=0;i<task->pairs.size();++i){
                if(!has_pending){
                        has_pending=1;
                        pending_ch=task->pairs[i].ch;
                        pending_cnt=task->pairs[i].cnt;
                        continue;
                }
                if(task->pairs[i].ch==pending_ch){
                    pending_cnt+=task->pairs[i].cnt;
                    continue;
                }
                send_pair(pending_ch,pending_cnt);
                pending_ch=task->pairs[i].ch;
                pending_cnt=task->pairs[i].cnt;
            }
            if (has_pending && task->task_id==task_count-1){
                send_pair(pending_ch,pending_cnt);
                std::cout.flush();
            }
        }
    
       


    





















}

}

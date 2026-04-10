#include <nyuenc.h>

int num_files;
int file_start;
size_t num_tasks;
int thread_cnt;
size_t* file_sizes;
std::deque<Task> q;
int task_id=0;
// class Task{
//     public:
//     int task_id;
//     size_t start;//代表在文件里的字节位置，有可能很大很大
//     size_t num_bytes;
//     uint8_t* data;
//     std::vector<Pair> pairs{};
//     Task(int tid):task_id(tid){}
// }


int main (int argc,char** argv){
    if (argc < 2) {
        return 1;
    }
    if (argv[1][0] == 'j' || (argv[1][0] == '-' && argv[1][1] == 'j')) {
        if (argc < 4) {
            return 1;
        }
        num_files=argc-3;
        file_start=3;
        thread_cnt=argv[2][0]-'0';
    }
    else{
        num_files=argc-1;
        file_start=1;
        thread_cnt=1;
    }
    file_sizes= new size_t[num_files]();
    uint8_t** file_map = new uint8_t*[num_files]();

    for(size_t i=0;i<num_files;++i){
        int fd=open(argv[file_start+i],O_RDONLY);
        if (fd < 0) {
            return 1;
        }
        auto st= struct stat{};
        if (fstat(fd,&st) != 0) {
            close(fd);
            return 1;
        }
        size_t file_size=st.st_size;
        file_sizes[i]=file_size;
        num_tasks+=(file_size+max_task_size-1)/(max_task_size);
        uint8_t* file_cursor = nullptr;
        if (file_size > 0) {
            file_cursor = static_cast<uint8_t*>(mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
            if (file_cursor == MAP_FAILED) {
                close(fd);
                return 1;
            }
        }
        file_map[i]=file_cursor;
        close(fd);
    }
    for(size_t i=0;i<num_files;i++){
        for(size_t start=0;start<file_sizes[i];start+=max_task_size){
            size_t remain=file_sizes[i]-start;
            Task t(task_id);
            t.data=file_map[i];
            t.start=start;
            t.num_bytes=(remain<max_task_size) ? remain : max_task_size;
            q.push_back(t);
            task_id++;
        }
    }
    return 0;
}

#include "config.hh"
#include <unistd.h>
#include "sim/trace.hh"
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <chrono>

namespace SimpleSSD{

namespace ERROR{

    const char NAME_LAYER[] = "layer";
    const char NAME_WL[] = "wl";
    const char NAME_RETENTION[] = "retention";
    const char NAME_PE[] = "pe";
    const char NAME_ISRETRY[] = "is_retry";
    const char NAME_RETENTIONMODE[] = "retention_mode";
    const char NAME_MEAN[] = "mean";
    const char NAME_SIGMA[] = "sigma";

    int _16_10_(unsigned char buma) { 
        unsigned char fanma=0;
        signed char yuanma=0;
        unsigned char index,temp=0;	
        
        fanma=buma-1;
        
        for(index=0;index<7;index++)
        {
            temp=fanma>>index;
            temp=~temp;
            temp&=0x01;
            temp=temp<<index;
            yuanma+=temp;
        }
        
        if(fanma&0x80)
        {
            yuanma=-yuanma;
        }

        return yuanma;
    }

    int hex_to_10(std::string str){
        unsigned char a = 0x0;
        for(int i=0;i<2;i++){
            if(str[i] == '0'){
            a += 0x0;
            }
            else if(str[i] == '1'){
                a += 0x1;
            }
            else if(str[i] == '2'){
                a += 0x2;
            }
            else if(str[i] == '3'){
                a += 0x3;
            }
            else if(str[i] == '4'){
                a += 0x4;
            }
            else if(str[i] == '1'){
                a += 0x1;
            }
            else if(str[i] == '5'){
                a += 0x5;
            }
            else if(str[i] == '6'){
                a += 0x6;
            }
            else if(str[i] == '7'){
                a += 0x7;
            }
            else if(str[i] == '8'){
                a += 0x8;
            }
            else if(str[i] == '1'){
                a += 0x1;
            }
            else if(str[i] == '1'){
                a += 0x1;
            }
            else if(str[i] == '9'){
                a += 0x1;
            }
            else if(str[i] == 'A'){
                a += 0xA;
            }
            else if(str[i] == 'B'){
                a += 0xB;
            }
            else if(str[i] == 'C'){
                a += 0xC;
            }
            else if(str[i] == 'D'){
                a += 0xD;
            }
            else if(str[i] == 'E'){
                a += 0xE;
            }
            else if(str[i] == 'F'){
                a += 0xF;
            }
            if(i == 0){
                a = a<<4;
            }
        }
        return _16_10_(a);
    }

    float string_to_float(std::string str){
        int i=0,len=str.length();
                float sum=0;
                bool ifneg=0;
                while(i<len){
                    if(str[i]=='-'){
                        ++i;
                        ifneg=1;
                    }
                    if(str[i]=='.') break;
                    sum=sum*10+str[i]-'0';
                    ++i;
                }//整数部分转化
                ++i;
                float t=1,d=1;
                bool ife=0;
                while(i<len){
                    if(str[i]=='e'){
                        ife=1;
                        break;
                    }
                    d*=0.1;
                    t=str[i]-'0';
                    sum+=t*d;
                    ++i;
                }//小数部分转化
                ++i;
                int mi=0;
                bool ifmineg=0;
                while(i<len){
                    if(str[i]=='-'){
                        ++i;
                        ifmineg=1;
                    }
                    mi=mi*10+str[i]-'0';
                    ++i;
                }//若是科学计数法的数据，指数部分转�??
                if(ifmineg==1||ife==1){
                    sum=sum*std::pow(10,-1*mi);
                }
                else if(ifmineg==0||ife==1){
                    sum=sum*std::pow(10,mi);
                }
                if(ifneg==1){
                    sum=-1*sum;
                }
                return sum;
    }

    Config::Config(){
        block = 495;
        layer = 128;
        wl = 6;
        page = 3;
        PE = 3000;
        pagesize = 80340;
        // char path[_MAX_PATH];
        // getcwd(path, _MAX_PATH);
        // std::string pathstr = path;
        // int pos = pathstr.find("simplessd-standalone");
        // pathstr = pathstr.substr(0, pos + 21);
        
        bdifference_file= "/home/yi/simplessd-new/simplessd_standalone/simplessd/error/config/block_difference.csv";
        is_retry = 1;
        // ldifference_file= "./simplessd/error/config/layer_difference.csv";
        read_csv(bdifference_file, block_table);
        
        // read_csv(ldifference_file, layer_table);
    }

    bool Config::setConfig(const char *name, const char *value){
        bool ret = true;            
        if(MATCH_NAME(NAME_LAYER)){
            layer = strtoul(value, nullptr, 10);
        }
        else if(MATCH_NAME(NAME_PE)){
            PE = strtoul(value, nullptr, 10);
        }
        else if(MATCH_NAME(NAME_WL)){
            wl = strtoul(value, nullptr, 10);
        }
        else if(MATCH_NAME(NAME_ISRETRY)){
            is_retry = strtoul(value, nullptr, 10);
        }
        else if(MATCH_NAME(NAME_RETENTION)){
            retention = value;
        }
        else if(MATCH_NAME(NAME_RETENTIONMODE)){
            retention_mode = strtoul(value, nullptr, 10);
        }
        else if(MATCH_NAME(NAME_MEAN)){
            mean = strtof(value, nullptr);
        }
        else if(MATCH_NAME(NAME_SIGMA)){
            sigma = strtof(value, nullptr);
        }
        else{
            ret = false;
        }

        return ret;
    }

    uint32_t Config::update(SimpleSSD::PAL::Config palConfig){
        block = palConfig.readUint(4);
        page = palConfig.readUint(5) / layer / wl;
        if(page * layer * wl != palConfig.readUint(5)){
            panic("Please check whether the configuration of layer, wl and Page is reasonable");
        }
        pagesize = palConfig.readUint(6)*8;
        uint32_t i = palConfig.readUint(6);
        NAND_type = palConfig.readInt(14);
        if(is_retry){
            if(retention_mode == 0){
                retentions_map.insert({0, retention});
            }
            else if(retention_mode == 1){
                read_csv();
            }
            else{
                read_csv(0);
            }
            check_layerdata();
        }
        
        
        return i;
    }

    void Config::read_csv(std::string file_name, difference &table){
        std::ifstream csv_data(file_name, std::ios::in);
        std::string line;

        if (!csv_data.is_open())
        {
            std::cout << file_name << "  Error: opening file fail" << std::endl;
            std::exit(1);
        }

        std::istringstream sin;         //将整行字符串line读入到字符串istringstream�??
        std::vector<std::string> words; //声明一个字符串向量
        std::string word;

        // 读取标题�??
        std::getline(csv_data, line);
        // 读取数据
        int layer_ = 0;
        while (std::getline(csv_data, line))
        {
            sin.clear();
            sin.str(line);
            words.clear();
            words.shrink_to_fit();
            int num = 0;
            while (std::getline(sin, word, ',')) //将字符串流sin中的字符读到field字符串中，以逗号为分隔符
            {
                num++;
                if(num == 2){
                    table[layer_] = string_to_float(word);
                }
                words.push_back(word); //将每一格中的数据逐个push
                // std::cout << atol(word.c_str());
            }
            layer_++;
            // do something。。�?
        }
        csv_data.close();
    }

    double Config::read_csv(uint32_t PE, uint32_t DR, uint32_t blockID, uint32_t pageID, uint32_t retry, double &ratio, double &mean_error, bool isopen){

        PageType pagetype = get_pageType(pageID);
        uint32_t layerID = get_layerID(pageID);
        std::string file_name;
        
        file_name = "./simplessd/error/config/Layer_Data/Layer_Data_";

        switch (NAND_type)
        {
            case 0:
                file_name += "SLC/";
                break;
            case 1:
                file_name += "MLC/";
                break;
            case 2:
                file_name += "TLC/";
                break;
            default:
                panic("NO such NAND type");
                break;
        }

        if(retry == 0){
            file_name += "Layer_Data_DefaultRead";
        }
        else{
            if(isopen){
                file_name += "open/Layer_Data_";
            }
            else{
                file_name += "close/Layer_Data_";
            }
            if(retry < 10){
                file_name += "ReadRetry0" + std::to_string(retry);
            }
            else{
                file_name += "ReadRetry" + std::to_string(retry);
            }
        }
        
        file_name += ".csv";
        
        std::ifstream csv_data(file_name, std::ios::in);
        std::string line;
 
        if (!csv_data.is_open())
        {
            std::cout<< file_name << "  Error: opening file fail" << std::endl;
            std::exit(1);
        }

        // std::cout<<"retry"<<std::endl;

        std::istringstream sin;         //将整行字符串line读入到字符串istringstream�??
        std::vector<int> DE; 
        std::string word;

        // 读取标题�??
        std::getline(csv_data, line);
        bool flag=0;
        // 读取数据
        while (std::getline(csv_data, line))
        {
            // std::cout<<line<<std::endl;
            sin.clear();
            sin.str(line);
            DE.clear();
            DE.shrink_to_fit();
            int num = 0;
            std::getline(sin, word, ',');
            bool flag_pe = 0;
            bool flag_dr = 0;
            bool flag_page = 0;
            double error = 0;
            while (std::getline(sin, word, ',')) //将字符串流sin中的字符读到field字符串中，以逗号为分隔符
            {
                // std::cout<<num<<":"<<word<<std::endl;
                if(num == 0 && !flag_pe){
                    if(uint32_t(std::stoi(word)) == PE){
                        flag_pe = 1;
                    }
                    else{
                        break;
                    }
                }
                if(num==1 && !flag_dr){
                    if(word == retentions_map[DR]){
                        flag_dr = 1;
                    }
                    else{
                        break;
                    }
                }
                if(num==2 && !flag_page){
                    if(std::stoi(word) == pagetype){
                        flag_page = 1;
                    }
                    else{
                        break;
                    }
                }

                if(uint32_t(num) == 3 + layerID && flag_page && flag_dr && flag_pe){
                    flag = 1;
                    error = std::stof(word) * block_table[blockID];
                }
                if(flag && num == int(layer) + 3){
                    ratio = error / (std::stof(word) * block_table[blockID]) ;
                    mean_error = std::stof(word) * block_table[blockID];
                }
                num++;
            }
            if(flag){
                break;
            }
            
        }
        // std::cout<<"result:"<<word<<std::endl;
        csv_data.close();
        return std::stof(word) * block_table[blockID];


    }

    RETRYTABLE Config::read_csv(std::string dir){
        // char path[_MAX_PATH];
        // getcwd(path, _MAX_PATH);
        // std::string pathstr = path;
        // int pos = pathstr.find("simplessd-standalone");
        // pathstr = pathstr.substr(0, pos + 21);
        std::ifstream csv_data(dir, std::ios::in);
        std::string line;
        RETRYTABLE retry_table;
        if (!csv_data.is_open())
        {
            std::cout << dir << " Error: opening file fail" << std::endl;
            std::exit(1);
        }

        // std::cout<<"retry_null"<<std::endl;

        std::istringstream sin;         //将整行字符串line读入到字符串istringstream�??
     
        std::string word;

        // 读取标题�??
        std::getline(csv_data, line);
        // 读取数据
        
        while (std::getline(csv_data, line))
        {
            sin.clear();
            sin.str(line);
            int num = 0;
            std::getline(sin, word, ',');
            while (std::getline(sin, word, ',')) //将字符串流sin中的字符读到field字符串中，以逗号为分隔符
            {
                retry_table[num].push_back(hex_to_10(word));
                num++;
            }
            
            
        }
        csv_data.close();
        return retry_table;
    }

    uint32_t Config::get_layerID(uint32_t pageID){
        return (pageID / page) / wl;
    }

    uint32_t Config::get_wlID(uint32_t pageID){
        return pageID % (page * wl) / page;
    }

    uint8_t Config::get_pageType(uint32_t pageID){
        if(NAND_type == 2)
            return (pageID <= 5) ? (uint8_t)LSB\
                            : ((pageID <= 7) ? (uint8_t)CSB
                                                : (((pageID - 8) >> 1) % 3));
        if(NAND_type == 1)
        {
            return pageID % 2;
        }

        if(NAND_type == 0){
            return 0;
        }
        return 0;
    }

    uint32_t Config::get_pagesize(){
        return pagesize;
    }

    uint32_t Config::get_pe(){
        return PE;
    }

    uint32_t Config::get_retention(uint32_t BlockID, uint64_t reqID){
        uint64_t seed = BlockID + reqID;
        if(retention_mode == 0){
            return 0;
        }
        else if(retention_mode == 1){
            std::default_random_engine e(seed);
            std::normal_distribution<double> distN(mean, sigma);
            double number;
            while(true){
                number = distN(e);
                if (number >= *min_element(retentions.begin(), retentions.end()) && number <= *max_element(retentions.begin(), retentions.end())){
                    break;
                }
            }

            uint32_t retention = uint32_t(std::lround(number));
            return retention;
        }
        else{
            srand(seed);
            double sj = rand() / double(RAND_MAX);
            auto first = retention_table.begin();
            auto end = retention_table.end();
            while(first != end){
                if(sj <= first->second){
                    return first->first; 
                }
                first++;
            }
        }
        return 0;
    }

    std::vector<voltage> Config::get_voltage(uint32_t pageID){
        PageType pagetype = get_pageType(pageID);
        std::vector<voltage> v;

        if(NAND_type == 0){
            v.push_back(R1);
            return v;
        }
        switch (pagetype)
        {
        case LSB:
            v.push_back(R1);
            v.push_back(R5);
            break;
        case MSB:
            v.push_back(R3);
            v.push_back(R7);
            break;
        case CSB:
            v.push_back(R2);
            v.push_back(R4);
            v.push_back(R6);
            break;
        default:
            break;
        }
        return v;
    }

    // SHIFTTABLE Config::get_SHIFT(uint32_t blockID, uint32_t pageID){
    //     SHIFTTABLE SHIFT;
    //     std::vector<voltage> v = get_voltage(pageID);
    //     for(int i=0; i<v.size(); i++){
    //         std::map<int, int> SHIFT_=read_csv(PE, retention, get_wlID(pageID), page);
    //         for(int j=-32; j<33; j++){
    //             SHIFT_[j] = SHIFT_[j] * block_table[blockID] * layer_table[get_layerID(pageID)];
    //         }
    //         SHIFT[v[i]] = SHIFT_;        
    //     }
        
    //     return SHIFT;
    // }

    RETRYTABLE Config::get_retry_table(bool flag){
        std::string dir;
        if(NAND_type == 0){
            if(flag == 0){
                dir = "./simplessd/error/config/retry_table/SLC/retry_table_SLC_close.csv";
            }
            else{
                dir = "./simplessd/error/config/retry_table/SLC/retry_table_SLC_open.csv";
            }
        }
        if(NAND_type == 1){
            if(flag == 0){
                dir = "./simplessd/error/config/retry_table/MLC/retry_table_MLC_close.csv";
            }
            else{
                dir = "./simplessd/error/config/retry_table/MLC/retry_table_MLC_open.csv";
            }
        }
        if(NAND_type == 2){
            if(flag == 0){
                dir = "./simplessd/error/config/retry_table/TLC/retry_table_TLC_close.csv";
            }
            else{
                dir = "./simplessd/error/config/retry_table/TLC/retry_table_TLC_open.csv";
            }
        }
        return read_csv(dir);
    }

    void Config::read_csv(){
        std::string file_name;

        
        file_name = "./simplessd/error/config/Layer_Data/Layer_Data_";

        switch (NAND_type)
        {
            case 0:
                file_name += "SLC/";
                break;
            case 1:
                file_name += "MLC/";
                break;
            case 2:
                file_name += "TLC/";
                break;
            default:
                panic("NO such NAND type");
                break;
        }
        file_name += "Layer_Data_DefaultRead.csv";
        
        std::ifstream csv_data(file_name, std::ios::in);
        std::string line;
 
        if (!csv_data.is_open())
        {
            std::cout<< file_name << "  Error: opening file fail" << std::endl;
            std::exit(1);
        }


        std::istringstream sin;         //将整行字符串line读入到字符串istringstream�??
        std::vector<int> DE; 
        std::string word;

        std::vector<std::string> DR;

        // 读取标题�??
        std::getline(csv_data, line);
        // 读取数据
        while (std::getline(csv_data, line))
        {
            // std::cout<<line<<std::endl;
            sin.clear();
            sin.str(line);
            DE.clear();
            DE.shrink_to_fit();
            int num = 0;
            std::getline(sin, word, ',');
            while (std::getline(sin, word, ',')) //将字符串流sin中的字符读到field字符串中，以逗号为分隔符
            {
                // std::cout<<num<<":"<<word<<std::endl;
                if(num==1){
                    DR.push_back(word);
                    break;
                }
                num++;
            }
        }
        // std::cout<<"result:"<<word<<std::endl;
        csv_data.close();
        sort(DR.begin(), DR.end());
        DR.erase(unique(DR.begin(), DR.end()), DR.end());
        for(uint32_t i=0;i<DR.size();i++){
            retentions_map.insert({i, DR[i]});
            retentions.push_back(i);
        }

    }

    void Config::read_csv(uint8_t mode){
        std::string file_name;
        if(mode == 0){
            file_name = "./simplessd/error/config/retention.csv";
        }else{
            file_name = "./simplessd/error/config/pe.csv";
        }
        
        std::ifstream csv_data(file_name, std::ios::in);
        std::string line;
 
        if (!csv_data.is_open())
        {
            std::cout<< file_name << "  Error: opening file fail" << std::endl;
            std::exit(1);
        }


        std::istringstream sin;         //将整行字符串line读入到字符串istringstream�??
        std::string word;

        std::vector<uint32_t> DR;

        // 读取标题�??
        std::getline(csv_data, line);
        int number = 0;
        // 读取数据
        while (std::getline(csv_data, line))
        {
            // std::cout<<line<<std::endl;
            sin.clear();
            sin.str(line);
            std::getline(sin, word, ',');
            std::string name(word);
            retentions_map.insert({number, name});
            retentions.push_back(number);
            std::getline(sin, word, ',');
            float probility = std::stof(word);
            retention_table.insert({number, probility});
            number++;
        }
        // std::cout<<"result:"<<word<<std::endl;
        csv_data.close();
        auto first = retention_table.begin();
        auto end = retention_table.end();
        auto pro = first;
        first++;
        while(first!= end){
            first->second += pro->second;
            pro = first;
            first++;
        }
        if(pro->second > 1){
            panic("The sum of all probabilities should be 1, and the sum of probabilities for this configuration sum is greater than 1.");
        }
        else if(pro->second < 1){
            panic("The sum of all probabilities should be 1, and the sum of probabilities for this configuration sum is less than 1.");
        }
    }

    void Config::check_layerdata(){
        std::cout<<"start check"<<std::endl;
        std::string file;
        file = "./simplessd/error/config/Layer_Data/Layer_Data_";

        switch (NAND_type)
        {
            case 0:
                file += "SLC/";
                break;
            case 1:
                file += "MLC/";
                break;
            case 2:
                file += "TLC/";
                break;
            default:
                panic("NO such NAND type");
                break;
        }

        for(int isopen=0; isopen<2; isopen++){
            RETRYTABLE retry_table = get_retry_table(isopen);
            max_retry = retry_table[0].size();
            std::vector<std::string> DR;
            for(int retry=0; retry<max_retry; retry++){
                std::string file_name = file;
                DR.clear();

                if(retry == 0){
                    file_name += "Layer_Data_DefaultRead";
                }
                else{
                    if(isopen){
                        file_name += "open/Layer_Data_";
                    }
                    else{
                        file_name += "close/Layer_Data_";
                    }
                    if(retry < 10){
                        file_name += "ReadRetry0" + std::to_string(retry);
                    }
                    else{
                        file_name += "ReadRetry" + std::to_string(retry);
                    }
                }

                file_name += ".csv";
                std::ifstream csv_data(file_name, std::ios::in);
                std::string line;
        
                if (!csv_data.is_open())
                {
                    file_name += "  Error: The file is missing, please check the corresponding folder.";
                    panic(file_name.c_str());
                }

                // std::cout<<"retry"<<std::endl;

                std::istringstream sin;         //将整行字符串line读入到字符串istringstream�??
                std::vector<int> DE; 
                std::string word;
                std::vector<int> pages;

                // 读取标题�??
                std::getline(csv_data, line);
                // 读取数据
                while (std::getline(csv_data, line))
                {
                    // std::cout<<line<<std::endl;
                    sin.clear();
                    sin.str(line);
                    DE.clear();
                    DE.shrink_to_fit();
                    int num = 0;
                    std::getline(sin, word, ',');
                    while (std::getline(sin, word, ',')) //将字符串流sin中的字符读到field字符串中，以逗号为分隔符
                    {
                        if(num==1){
                            DR.push_back(word);
                        }
                        if(num==2){
                            pages.push_back(std::stoi(word));
                            break;
                        }
                        num++;
                    }
                    
                }
                csv_data.close();
                sort(DR.begin(), DR.end());
                DR.erase(unique(DR.begin(), DR.end()), DR.end());
                if(retention_mode == 2){
                    if(retentions.size() != DR.size()){
                        panic("The number of retentions in Layer_Data does not match the number in retention.csv, Layer_Data: %d, retention.csv: %d", DR.size(), retentions.size());
                    }
                    auto first = retentions_map.begin();
                    auto end = retentions_map.end();
                    int number=0;
                    while(first!=end){
                        if(first->second != DR[number]){
                            panic("retention types %s not appearing in %s", first->second, file_name.c_str());
                        }
                        first++;
                        number++;
                    } 
                }
                

                switch (NAND_type)
                {
                case 0:
                    for(uint32_t i=0; i<pages.size(); i++){
                        if(pages[i] != 0){
                            file_name += (" ERROR: There is an error in the page in " + std::to_string(i) + "th line of this file. Please make modifications.");
                            panic(file_name.c_str());
                        }
                    }
                    break;
                case 1:
                    for(uint32_t i=0; i<pages.size(); i+=2){
                        if(pages[i] != 0 or pages[i+1] != 1 or pages[i+2] != 2){
                            file_name += (" ERROR: There is an error in the page in " + std::to_string(i) + "th line of this file. Please make modifications.");
                            panic(file_name.c_str());   
                        }
                    }

                    break;
                case 2:
                    for(uint32_t i=0; i<pages.size(); i+=3){
                        if(pages[i] != 0 or pages[i+1] != 1 or pages[i+2] != 2){
                            file_name += (" ERROR: There is an error in the page in " + std::to_string(i) + "th line of this file. Please make modifications.");
                            panic(file_name.c_str());   
                        }
                    }
                    break;
                
                default:
                    panic("No such NAND type");
                    break;
                }
            }
            
        }
    }

}
}

#include "retryStatistics.hh"

#include <fstream>
#include <iostream>
namespace SimpleSSD {
namespace ERROR {

RetryStatistics::RetryStatistics(ConfigReader &c) : conf(c) {
  requestsNumber = 0;
  retryRequestsNumber = 0;
}
RetryStatistics::~RetryStatistics() {
  if (getRetryModule()) {
    showStat();
    // showRetryRequest();
    // showRetryLatency();
    writerToFile();
  }

  retryRequests.clear();
}
void RetryStatistics::closeRetry(::CPDPBP address, uint64_t tick) {
  requestsNumber++;
  uint32_t tempLen = retryRequests.size();
  bool isSame = false;
  for (uint32_t i = 0; i < tempLen; i++) {
    ::CPDPBP tempAddress = retryRequests[i].getAddress();
    bool tempRetryCompleted = retryRequests[i].getRetryCompleted();
    isSame = isSameAddress(address, tempAddress, 3) && !tempRetryCompleted;
    if (isSame) {
      retryRequests[i].setRetryCompleted();
      retryRequests[i].addRetry(tick);
      uint32_t count = retryRequests[i].getRetryCount();
      printCPDPBP(address, count);
      retryRequestsNumber++;
      break;
    }
  }
}
void RetryStatistics::closeRetry(::CPDPBP address, uint64_t tick,
                                 uint64_t start, bool readSuccess) {
  requestsNumber++;
  uint32_t tempLen = retryRequests.size();
  bool isSame = false;
  for (uint32_t i = 0; i < tempLen; i++) {
    ::CPDPBP tempAddress = retryRequests[i].getAddress();
    bool tempRetryCompleted = retryRequests[i].getRetryCompleted();
    isSame = isSameAddress(address, tempAddress, 3) && !tempRetryCompleted;
    if (isSame) {
      retryRequests[i].setRetryCompleted();
      retryRequests[i].addRetry(tick);
      if (!readSuccess) {
        retryRequests[i].setReadState(readSuccess);
      }
      uint32_t count = retryRequests[i].getRetryCount();
      printCPDPBP(address, count);
      debugprint(
          LOG_ERROR,
          "compare:retryList_sum %llu|finished_retrytick-enter_retrytick %llu",
          retryRequests[i].getRetryTime(), tick - start);
      retryRequestsNumber++;
      break;
    }
  }
  if (!isSame) {
    RetryPage item = RetryPage(address, start);
    item.setRetryCompleted();
    retryRequests.push_back(item);
  }
}
void RetryStatistics::addRetry(::CPDPBP address, uint64_t tick) {
  bool isSame = false;
  uint32_t tempLen = retryRequests.size();
  uint32_t place = -1;
  uint32_t count = 0;
  for (uint32_t i = 0; i < tempLen; i++) {
    ::CPDPBP tempAddress = retryRequests[i].getAddress();
    bool tempRetryCompleted = retryRequests[i].getRetryCompleted();
    isSame = isSameAddress(address, tempAddress, 3) && !tempRetryCompleted;
    if (isSame) {
      place = i;
      break;
    }
  }
  if (isSame) {
    retryRequests[place].addRetry(tick);
    count = retryRequests[place].getRetryCount();
  }
  else {
    retryRequests.push_back(RetryPage(address, tick));
    place = tempLen;
  }
  debugprint(LOG_ERROR, "| retryList_sum %llu",
             retryRequests[place].getRetryTime());
  // std::cout<<"|
  // retryList_sum"<<retryRequests[place].getRetryTime()<<std::endl;
  printCPDPBP(address, count);
}
void RetryStatistics::printCPDPBP(::CPDPBP addr, uint32_t count) {
  uint32_t layer = config.get_layerID(addr.Page);
  uint32_t type = config.get_pageType(addr.Page);
  debugprint(LOG_ERROR,
             "Read-Retry | C %5u | W %5u | D %5u | P %5u | B %5u | P %5u : "
             "Layer %5u ; PageType %5u;Number_Retryed %5u",
             addr.Channel, addr.Package, addr.Die, addr.Plane, addr.Block,
             addr.Page, layer, type, count);
}
bool RetryStatistics::isSameAddress(::CPDPBP address1, ::CPDPBP address2,
                                    uint32_t type) {
  bool result = false;
  switch (type) {
    case 0:
      if (address1.Channel == address2.Channel) {
        result = true;
      }
      break;
    case 1:
      if (address1.Channel == address2.Channel &&
          address1.Package == address2.Package &&
          address1.Die == address2.Die) {
        result = true;
      }
      break;
    case 2:
      if (address1.Channel == address2.Channel &&
          address1.Package == address2.Package &&
          address1.Die == address2.Die && address1.Plane == address2.Plane &&
          address1.Block == address2.Block) {
        result = true;
      }
      break;
    case 3:
    default:
      if (address1.Channel == address2.Channel &&
          address1.Package == address2.Package &&
          address1.Die == address2.Die && address1.Plane == address2.Plane &&
          address1.Block == address2.Block && address1.Page == address2.Page) {
        result = true;
      }
      break;
  }

  return result;
}

void RetryStatistics::showStat() {
  // std::cout << "********** Retry Statistics **********" << std::endl;
  showRetryRequest();
  showRetryDistribution();
  showRetryLatency();
}
void RetryStatistics::showRetryRequest() {

  debugprint(LOG_ERROR,
             "Total page requests %5u | Total Retry page requests %5u "
             "|Retry_Requests_List_size %5u | Percent %.2f %%",
             requestsNumber, retryRequestsNumber, retryRequests.size(),
             (float)retryRequestsNumber / (float)requestsNumber * 100);
  std::map<uint32_t, uint32_t> retryRequest;
  uint32_t retry_request_count=0;
  uint32_t tempLen = retryRequests.size();
  for (uint32_t i = 0; i < tempLen; i++) {
    uint32_t tempRetryCount = retryRequests[i].getRetryCount();
    if(tempRetryCount!=0){
      retry_request_count+=1;
    }
    auto item = retryRequest.find(tempRetryCount);
    if (item != retryRequest.end()) {
      retryRequest[item->first] = item->second + 1;
    }
    else {
      retryRequest.insert(std::pair<uint32_t, uint32_t>(tempRetryCount, 1));
    }
  }
  debugprint(LOG_ERROR,
             "Total page requests %5u | Total Retry page requests %5u "
             "|Retry_Requests_List_size %5u | Percent %.2f %%",
             requestsNumber,retry_request_count, retryRequests.size(),
             (float)retry_request_count / (float)requestsNumber * 100);
  std::map<uint32_t, uint32_t>::iterator iter;
  for (iter = retryRequest.begin(); iter != retryRequest.end(); iter++) {
    if (iter->second > 0)
      debugprint(LOG_ERROR,
                 "Retry number %5u | Retry  requests %5u | percent :%.2f%%\n",
                 iter->first, iter->second,
                 (float)iter->second / (float)tempLen * 100);
  }
  retryRequest.clear();
}
void RetryStatistics::showRetryDistribution() {
  std::map<uint32_t, uint32_t> retrylayer, retryPageType;
  std::map<::CPDPBP, uint32_t, MyCompare> retryChannel, retryDie, retryBlock,
      retryPage;
  uint32_t tempLen = retryRequests.size();
  for (uint32_t i = 0; i < tempLen; i++) {
    ::CPDPBP tempAddress = retryRequests[i].getAddress();
    uint32_t tempRetryCount = retryRequests[i].getRetryCount();
    uint32_t layer = config.get_layerID(tempAddress.Page);
    uint32_t type = config.get_pageType(tempAddress.Page);
    // debugprint(LOG_ERROR, "Retry pageLayer number %5u|pagetype %5u",
    // layer,type);
    auto itemLayer = retrylayer.find(layer);
    if (itemLayer != retrylayer.end()) {
      retrylayer[itemLayer->first] = itemLayer->second + tempRetryCount;
    }
    else {
      retrylayer.insert(std::pair<uint32_t, uint32_t>(layer, tempRetryCount));
      // debugprint(LOG_ERROR, "Retry pageLayer number %5u ", layer);
    }
    auto itemType = retryPageType.find(type);
    if (itemType != retryPageType.end()) {
      retryPageType[itemType->first] = itemType->second + tempRetryCount;
    }
    else {
      retryPageType.insert(std::pair<uint32_t, uint32_t>(type, tempRetryCount));
      // debugprint(LOG_ERROR, "pageType %5u ", type);
    }
  }
  std::map<uint32_t, uint32_t>::iterator iterInpage;
  for (iterInpage = retrylayer.begin(); iterInpage != retrylayer.end();
       iterInpage++) {
    if (iterInpage->second > 0)
      debugprint(LOG_ERROR, "Retry pageLayer number %5u | Retry  count %5u",
                 iterInpage->first, iterInpage->second);
  }
  for (iterInpage = retryPageType.begin(); iterInpage != retryPageType.end();
       iterInpage++) {
    if (iterInpage->second > 0)
      debugprint(LOG_ERROR, "Retry pageType  %5u | Retry  count %5u\n",
                 iterInpage->first, iterInpage->second);
  }
  retrylayer.clear();
  retryPageType.clear();
  std::map<::CPDPBP, uint32_t, MyCompare>::iterator iter;
  for (uint32_t i = 0; i < tempLen; i++) {
    ::CPDPBP tempAddress = retryRequests[i].getAddress();
    uint32_t tempRetryCount = retryRequests[i].getRetryCount();
    for (iter = retryChannel.begin(); iter != retryChannel.end(); iter++) {
      if (isSameAddress(iter->first, tempAddress, 0))
        break;
    }
    if (iter != retryChannel.end()) {
      retryChannel[iter->first] = iter->second + tempRetryCount;
    }
    else {
      retryChannel.insert(
          std::pair<::CPDPBP, uint32_t>(tempAddress, tempRetryCount));
    }
    for (iter = retryDie.begin(); iter != retryDie.end(); iter++) {
      if (isSameAddress(iter->first, tempAddress, 1))
        break;
    }
    if (iter != retryDie.end()) {
      retryDie[iter->first] = iter->second + tempRetryCount;
    }
    else {
      retryDie.insert(
          std::pair<::CPDPBP, uint32_t>(tempAddress, tempRetryCount));
    }
    for (iter = retryBlock.begin(); iter != retryBlock.end(); iter++) {
      if (isSameAddress(iter->first, tempAddress, 2))
        break;
    }
    if (iter != retryBlock.end()) {
      retryBlock[iter->first] = iter->second + tempRetryCount;
    }
    else {
      retryBlock.insert(
          std::pair<::CPDPBP, uint32_t>(tempAddress, tempRetryCount));
    }
    for (iter = retryPage.begin(); iter != retryPage.end(); iter++) {
      if (isSameAddress(iter->first, tempAddress, 3))
        break;
    }
    if (iter != retryPage.end()) {
      retryPage[iter->first] = iter->second + tempRetryCount;
    }
    else {
      retryPage.insert(
          std::pair<::CPDPBP, uint32_t>(tempAddress, tempRetryCount));
    }
  }
  for (iter = retryChannel.begin(); iter != retryChannel.end(); iter++) {
    if (iter->second > 0)
      debugprint(LOG_ERROR, "Channel : %5u | Retry  count %5u",
                 iter->first.Channel, iter->second);
  }
  for (iter = retryDie.begin(); iter != retryDie.end(); iter++) {
    if (iter->second > 0)
      debugprint(LOG_ERROR,
                 "Channel : %5u | Package: %5u | Die: %5u | Retry  count %5u",
                 iter->first.Channel, iter->first.Package, iter->first.Die,
                 iter->second);
  }
  for (iter = retryBlock.begin(); iter != retryBlock.end(); iter++) {
    if (iter->second > 0)
      debugprint(LOG_ERROR,
                 "Channel : %5u | Package: %5u | Die: %5u | Plane %5u |Block: "
                 "%5u | Retry  count %5u",
                 iter->first.Channel, iter->first.Package, iter->first.Die,
                 iter->first.Plane, iter->first.Block, iter->second);
  }
  for (iter = retryPage.begin(); iter != retryPage.end(); iter++) {
    if (iter->second > 0)
      debugprint(LOG_ERROR,
                 "Channel : %5u | Package: %5u | Die: %5u | Plane %5u |Block: "
                 "%5u |Page: %5u | Retry  count %5u",
                 iter->first.Channel, iter->first.Package, iter->first.Die,
                 iter->first.Plane, iter->first.Block, iter->first.Page,
                 iter->second);
  }
  retryChannel.clear();
  retryDie.clear();
  retryBlock.clear();
  retryPage.clear();
}
void RetryStatistics::showRetryLatency() {
  uint64_t totalRetryTime = 0;
  uint32_t tempLen = retryRequests.size();
  for (uint32_t i = 0; i < tempLen; i++) {
    totalRetryTime += retryRequests[i].getRetryTime();
  }
  debugprint(LOG_ERROR, "Total retry Time %llu\n", totalRetryTime);
}
void RetryStatistics::setRetryModule(bool retry) {
  retryModule = retry;
}
bool RetryStatistics::getRetryModule() {
  return retryModule;
}
void RetryStatistics::setRequestNumber(uint32_t number) {
  requestsNumber = number;
}
void RetryStatistics::setRetryRequestNumber() {
  retryRequestsNumber++;
}
void RetryStatistics::writerToFile() {
  std::string file_name = "./simplessd/error/retry_statistics.csv";
  std::ofstream outfile;
  outfile.open(file_name, std::ios::out);
  if (!outfile.is_open()) {
    std::cout << "Error: opening file fail" << std::endl;
    std::exit(1);
  }

  uint32_t tempLen = retryRequests.size();
  uint32_t retry_request_count=0;
  for (uint32_t i = 0; i < tempLen; i++) {
    if(retryRequests[i].getRetryCount()!=0){
      retry_request_count+=1;
    }
  }
  //uint32_t MAX_RETRY = config.max_retry;
  bool read_success = true;
  outfile << "Total Requests,Retry Requests" << std::endl;
  outfile << requestsNumber << "," << retry_request_count << std::endl;
  outfile << "Channel,Package,Die,Plane,Block,Page,PageLayerNumber,PageType,"
             "RetryNumber,RetryTime,ReadState(0:read fail)"
          << std::endl;
  for (uint32_t i = 0; i < tempLen; i++) {
    ::CPDPBP addr = retryRequests[i].getAddress();
    uint32_t count = retryRequests[i].getRetryCount();
    uint64_t time = 0;
    if (count != 0) {
      time=retryRequests[i].getRetryTime();
    }
    read_success= retryRequests[i].getReadState();
    uint32_t layer = config.get_layerID(addr.Page);
    uint32_t type = config.get_pageType(addr.Page);
    outfile << addr.Channel << "," << addr.Package << "," << addr.Die << ","
            << addr.Plane << "," << addr.Block << "," << addr.Page << ","
            << layer << "," << type << "," << count << "," << time <<","<<read_success<< std::endl;
  }
  outfile.close();
}

}  // namespace ERROR
}  // namespace SimpleSSD
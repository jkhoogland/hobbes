/*
 * batchrecv : write local structured log files from compressed transaction data sent over a network
 */

#ifndef HOG_BATCHRECV_H_INCLUDED
#define HOG_BATCHRECV_H_INCLUDED

#include <string>
#include <functional>
#include <thread>

namespace hog {

std::thread pullRemoteDataT(const std::string& dir, const std::string& listenport);
bool pullRemoteData(const std::string& dir, const std::string& listenport);

}

#endif


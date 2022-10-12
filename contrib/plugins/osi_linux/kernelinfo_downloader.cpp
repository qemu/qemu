#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>


static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

int download_kernelinfo(const char *file, const char *group){
	// we'll create a file, but we won't make one without a filename
	if  (file == NULL)
		return -1;
	std::cout << "Attempting to download kernelinfo.conf from panda-re.mit.edu... ";
	CURL *curl;
	CURLcode res;
	std::string url = "https://panda-re.mit.edu/kernelinfos/";
	url.append(group);
	url.append(".conf");
	std::string readBuffer;

	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		if (readBuffer.length() > 0 && res == CURLE_OK){
	  		std::ofstream kernelinfo_file;
	  		kernelinfo_file.open (file, std::ifstream::app);
	  		kernelinfo_file << "\n" << readBuffer << std::endl;
	  		kernelinfo_file.close();
        std::cout << " OK" << std::endl;
	  		return 0;
		}else if(res == CURLE_HTTP_RETURNED_ERROR) {
			std::cout << " FAIL: config not found on server" << std::endl;
    }else{
			std::cout << " FAIL: error" << std::endl;
		}
	}
	return -1;
}


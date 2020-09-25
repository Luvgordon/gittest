# project name 
PROJECT(lsitool) 
include_directories(${PROJECT_SOURCE_DIR}/lsi ${PROJECT_SOURCE_DIR}/inc) 
AUX_SOURCE_DIRECTORY(. SRC_LIST)  
ADD_LIBRARY(lsitool STATIC ${SRC_LIST}) 
 

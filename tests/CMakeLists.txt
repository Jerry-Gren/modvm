# tests/CMakeLists.txt

# 自动收集当前目录下的所有测试源文件
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "*.c")

# 遍历所有收集到的测试文件
foreach(test_source IN LISTS TEST_SOURCES)
    # 提取无后缀的文件名 (例如: test_infra.c -> test_infra)
    get_filename_component(test_name ${test_source} NAME_WE)
    
    # 自动创建可执行文件
    add_executable(${test_name} ${test_source})
    
    # 直接链接前面我们配置的 OBJECT 核心库
    target_link_libraries(${test_name} PRIVATE modvm_core)
    
    # 自动注册到 CTest (测试名称会自动映射为文件名)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
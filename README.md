# hku_rest

简易的nng http restful server，快速原型开发

简单吞吐性能测试使用 wrk ,模拟10个连接，压测30秒，输出统计结果

wrk -t10 -c100 -d30s http://localhost:8080/hello

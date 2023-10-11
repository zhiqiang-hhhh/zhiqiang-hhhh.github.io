# Aggregate Model
在数据插入的过程中按照建表预设的规则进行 Aggregate Computation。


Query:
    Summary:
          -  Profile  ID:  4a942b46ff9b486b-a6cce4157f623ff6
          -  Total:  2s50ms

    Fragments:
        Fragment  0:
            Instance  (1):
                VDataBufferSender  (dst_fragment_instance_id=4a942b46ff9b486b--59331bea809dbfd9):
                        -  AppendBatchTime:  2s39ms
                            -  CopyBufferTime:  0ns
                            -  ResultSendTime:  128.97ms
                            -  TupleConvertTime:  1s874ms
                        -  BytesSent:  108.16  MB
                        -  NumSentRows:  1.00001M  (1000010)

Query:
    Summary:
          -  Profile  ID:  9ec6bab0fb5b4517-bcc7691a1defcd4d
          -  Total:  2s73ms
          -  Sql  Statement:  select  *    from  tbl
    Fragments:
        Fragment  0:
            Instance  (1):
                VDataBufferSender  (dst_fragment_instance_id=-6139454f04a4bae9--433896e5e2103282):
                        -  AppendBatchTime:  2s55ms
                            -  CopyBufferTime:  0ns
                            -  ResultSendTime:  128.830ms
                            -  TupleConvertTime:  1s889ms
                        -  BytesSent:  108.16  MB
                        -  NumSentRows:  1.00001M  (1000010)


Query:
    Summary:
          -  Profile  ID:  708111a2d5347b7-8b707bf99ccf4ce3
          -  Total:  2s341ms
          -  Sql  Statement:  select  *    from  tbl
    Fragments:
        Fragment  0:
            Instance  (1):
                VDataBufferSender  (dst_fragment_instance_id=708111a2d5347b7--748f84066330b2ec):
                        -  AppendBatchTime:  2s311ms
                              -  CopyBufferTime:  0ns
                              -  ResultSendTime:  153.229ms
                              -  TupleConvertTime:  2s114ms
                        -  BytesSent:  108.16  MB
                        -  NumSentRows:  1.00001M  (1000010)


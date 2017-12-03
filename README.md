# mcron

#Run every 1 sec
(job '(next-second (range 0 60 10)) "your command")

#Run on 15 sec of each minute 1:15
(job
    '(next-second-from
       (next-minute)
       '(15))
    "your command")


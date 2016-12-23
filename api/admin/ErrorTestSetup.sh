#!/bin/bash
KAFKA_PATH="/root/Downloads/kafka_2.11-0.10.0.1"
KAFKA_CONFIG="config/server.properties"
TOPIC="err_test_topic"
STREAM="/gtest-ErrorCodeTest0"
ZOOKEEPER="localhost:5181"

run()
{
   echo "\$ $@" ; "$@" ;
}


echo "Creating stream $STREAM  with autocreate topic 'off'"
run maprcli stream create -path $STREAM -autocreate false
sleep 2
echo "Creating topic $stream_topic"
run maprcli stream topic create -path $STREAM -topic $TOPIC

echo "Set kafka auto create to 'false'"
run echo 'auto.create.topics.enable = false' >> $KAFKA_PATH/$KAFKA_CONFIG

echo "Starting kafka server, kafka package at $KAFKA_PATH"
rm -rf /tmp/kafka-logs/
run $KAFKA_PATH/bin/kafka-server-start.sh $KAFKA_PATH/$KAFKA_CONFIG >/dev/null &
sleep 5

echo "Create Kafka topic"
run $KAFKA_PATH/bin/kafka-topics.sh --zookeeper $ZOOKEEPER --create --topic $TOPIC --partitions 1 --replication-factor 1


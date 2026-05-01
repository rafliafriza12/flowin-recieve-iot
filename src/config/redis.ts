import { Redis } from "@upstash/redis";
import { configDotenv } from "dotenv";

configDotenv();

if (!process.env.UPSTASH_REDIS_REST_URL || !process.env.UPSTASH_REDIS_REST_TOKEN) {
  throw new Error(
    "UPSTASH_REDIS_REST_URL / UPSTASH_REDIS_REST_TOKEN is not defined. Please check your .env file"
  );
}

export const redis = new Redis({
  url: process.env.UPSTASH_REDIS_REST_URL,
  token: process.env.UPSTASH_REDIS_REST_TOKEN,
});

export const IOT_KEY_PREFIX = "iot:";
export const SEVEN_DAYS_SECONDS = 7 * 24 * 60 * 60;

export const buildIotKey = (userId: string, meterId: string): string =>
  `${IOT_KEY_PREFIX}${userId}:${meterId}`;

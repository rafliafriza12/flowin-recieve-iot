import mongoose from "mongoose";
import { configDotenv } from "dotenv";

configDotenv();

interface MongooseCache {
  conn: typeof mongoose | null;
  promise: Promise<typeof mongoose> | null;
}

declare global {
  // eslint-disable-next-line no-var
  var _mongooseCache: MongooseCache | undefined;
}

const cache: MongooseCache =
  global._mongooseCache ?? (global._mongooseCache = { conn: null, promise: null });

const clientOptions: mongoose.ConnectOptions = {
  serverApi: { version: "1" as const, strict: true, deprecationErrors: true },
  retryWrites: true,
  w: "majority",
  maxPoolSize: 5,
  serverSelectionTimeoutMS: 5000,
  socketTimeoutMS: 45000,
  bufferCommands: false,
};

const connectDB = async (): Promise<typeof mongoose> => {
  if (cache.conn) return cache.conn;

  if (!process.env.MONGO_URI) {
    throw new Error("MONGO_URI environment variable is not defined");
  }

  if (!cache.promise) {
    cache.promise = mongoose.connect(process.env.MONGO_URI, clientOptions);
  }

  try {
    cache.conn = await cache.promise;
  } catch (error) {
    cache.promise = null;
    throw error;
  }

  return cache.conn;
};

export default connectDB;

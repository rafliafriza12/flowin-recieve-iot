import { Request, Response } from "express";
import { redis, IOT_KEY_PREFIX } from "../config/redis";
import RiwayatPenggunaan from "../models/RiwayatPenggunaan";
import connectDB from "../config/database";

const SEVEN_DAYS_MS = 7 * 24 * 60 * 60 * 1000;

interface StoredEntry {
  usedWater: number;
  ts: number;
}

const parseEntry = (raw: unknown): StoredEntry | null => {
  try {
    const obj = typeof raw === "string" ? JSON.parse(raw) : (raw as StoredEntry);
    if (
      obj &&
      typeof obj.usedWater === "number" &&
      typeof obj.ts === "number"
    ) {
      return obj as StoredEntry;
    }
    return null;
  } catch {
    return null;
  }
};

const parseKey = (key: string): { userId: string; meterId: string } | null => {
  const rest = key.slice(IOT_KEY_PREFIX.length);
  const parts = rest.split(":");
  if (parts.length !== 2) return null;
  return { userId: parts[0], meterId: parts[1] };
};

class CronController {
  public migrate = async (req: Request, res: Response): Promise<void> => {
    try {
      const auth = req.headers.authorization;
      const expected = process.env.CRON_SECRET;
      if (!expected || auth !== `Bearer ${expected}`) {
        res.status(401).json({ status: 401, message: "Unauthorized" });
        return;
      }

      await connectDB();

      const cutoff = Date.now() - SEVEN_DAYS_MS;
      let cursor: string | number = 0;
      let totalMigrated = 0;
      let scannedKeys = 0;

      do {
        const result = await redis.scan(cursor, {
          match: `${IOT_KEY_PREFIX}*`,
          count: 100,
        });
        const nextCursor = result[0] as string | number;
        const keys = result[1] as string[];
        cursor = nextCursor;

        for (const key of keys) {
          scannedKeys++;
          const parsedKey = parseKey(key);
          if (!parsedKey) continue;

          const items = (await redis.lrange(key, 0, -1)) as unknown[];
          if (items.length === 0) continue;

          const toMigrate: StoredEntry[] = [];
          let keepStartIdx = 0;
          for (let i = 0; i < items.length; i++) {
            const entry = parseEntry(items[i]);
            if (entry && entry.ts < cutoff) {
              toMigrate.push(entry);
              keepStartIdx = i + 1;
            } else {
              break;
            }
          }

          if (toMigrate.length === 0) continue;

          const docs = toMigrate.map((e) => ({
            UserID: parsedKey.userId,
            MeterID: parsedKey.meterId,
            PenggunaanAir: e.usedWater,
            timestamp: new Date(e.ts),
          }));

          await RiwayatPenggunaan.insertMany(docs, { ordered: false });

          if (keepStartIdx >= items.length) {
            await redis.del(key);
          } else {
            await redis.ltrim(key, keepStartIdx, -1);
          }

          totalMigrated += docs.length;
        }
      } while (cursor !== 0 && cursor !== "0");

      res.status(200).json({
        status: 200,
        message: "Migration completed",
        scannedKeys,
        totalMigrated,
      });
    } catch (error) {
      console.error("Cron migrate error:", error);
      res.status(500).json({ status: 500, message: "Internal server error" });
    }
  };
}

export default new CronController();

import { Request, Response } from "express";
import { redis, buildIotKey, SEVEN_DAYS_SECONDS } from "../config/redis";

interface BatchEntry {
  usedWater: number;
  ts?: number;
}

interface IotRequestBody {
  usedWater?: number;
  batch?: BatchEntry[];
}

class IotController {
  public ingest = async (req: Request, res: Response): Promise<void> => {
    try {
      const userId = req.params.userId;
      const meterId = req.params.meterId;
      if (typeof userId !== "string" || typeof meterId !== "string" || !userId || !meterId) {
        res.status(400).json({ status: 400, message: "userId dan meterId wajib di URL" });
        return;
      }

      const body: IotRequestBody = req.body ?? {};
      const now = Date.now();

      const entries: BatchEntry[] = Array.isArray(body.batch) && body.batch.length > 0
        ? body.batch
        : typeof body.usedWater === "number"
          ? [{ usedWater: body.usedWater, ts: now }]
          : [];

      if (entries.length === 0) {
        res.status(400).json({
          status: 400,
          message: "Body harus berisi { usedWater: number } atau { batch: [{usedWater, ts?}] }",
        });
        return;
      }

      const key = buildIotKey(userId, meterId);
      const payloads = entries
        .filter((e) => typeof e.usedWater === "number" && !isNaN(e.usedWater))
        .map((e) =>
          JSON.stringify({
            usedWater: e.usedWater,
            ts: typeof e.ts === "number" ? e.ts : now,
          })
        );

      if (payloads.length === 0) {
        res.status(400).json({ status: 400, message: "Tidak ada entry valid" });
        return;
      }

      await redis.rpush(key, ...(payloads as [string, ...string[]]));
      await redis.expire(key, SEVEN_DAYS_SECONDS);

      res.status(201).json({
        status: 201,
        message: "Data tersimpan di Redis",
        userId,
        meterId,
        inserted: payloads.length,
      });
    } catch (error) {
      console.error("IoT ingest error:", error);
      res.status(500).json({ status: 500, message: "Internal server error" });
    }
  };
}

export default new IotController();

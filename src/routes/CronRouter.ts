import express from "express";
import CronController from "../controllers/CronController";

const router = express.Router();

router.get("/migrate", CronController.migrate);

export default router;

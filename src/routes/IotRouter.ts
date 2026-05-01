import express from "express";
import IotController from "../controllers/IotController";

const router = express.Router();

router.post("/:userId/:meterId", IotController.ingest);

export default router;

export const ATLAS_LIMIT: number;
export const PIXELS: number;
export interface Clip {id:number;name?:string;count:number;ms:number;loop:boolean;start?:number}
export function packLz4(src:Uint8Array):Uint8Array;
export function unpackLz4(src:Uint8Array,size?:number):Uint8Array;
export function encodeAtlas(clips:Clip[],frames:Uint8Array[]):Uint8Array;
export function validAnimationAtlas(bytes:Uint8Array):boolean;
export function quantize(frames:Uint8Array[]):{palette:number[][];frames:Uint8Array[]};
export function base64(bytes:Uint8Array):string;

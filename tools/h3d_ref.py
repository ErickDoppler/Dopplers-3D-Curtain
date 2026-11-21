# Reference tensors from ComfyUI's own Hunyuan3D 2.0 pipeline, to check the C++ port stage by stage.
# Run with ComfyUI's python:  python_embeded\python.exe tools\h3d_ref.py [image.png]
# Everything runs on the CPU (the GPU may be busy). Output: %TEMP%\h3d_ref\*.bin (float32, row-major) and meta.json (shapes).
import sys, os, json, time
ARGS = sys.argv[1:]; sys.argv = [sys.argv[0], '--cpu']
C = r"C:\workenv\ComfyUI-Easy-Install\ComfyUI-Easy-Install\ComfyUI"
sys.path.insert(0, C); os.chdir(C)
import numpy as np, torch
import comfy.sd, comfy.utils, comfy.clip_model, comfy.samplers, comfy.model_management as mm
from PIL import Image, ImageOps

out = os.path.join(os.environ['TEMP'], 'h3d_ref'); os.makedirs(out, exist_ok=True)
meta = {}
def dump(name, t):
    a = t.detach().float().cpu().numpy(); a.tofile(os.path.join(out, name + '.bin')); meta[name] = list(a.shape)
    json.dump(meta, open(os.path.join(out, 'meta.json'), 'w')); print('dumped', name, a.shape, 'mean %.5f std %.5f' % (a.mean(), a.std()), flush=True)

img_path = ARGS[0] if ARGS else os.path.join(C, 'input', 'BallerinaBunny.png')
img = ImageOps.exif_transpose(Image.open(img_path)).convert('RGB')
image = torch.from_numpy(np.array(img).astype(np.float32) / 255.0)[None]            # [1,H,W,3], like LoadImage
meta['image_path'] = img_path; meta['image_size'] = list(image.shape)
t0 = time.time()
model, _, vae, cv = comfy.sd.load_checkpoint_guess_config(os.path.join(C, 'models', 'checkpoints', 'hunyuan3d-dit-v2_fp16.safetensors'), output_vae=True, output_clip=False, output_clipvision=True)[:4]
print('loaded in %.1f s' % (time.time() - t0), flush=True)

# 1. image encoder (CLIPVisionEncode, crop none)
pv = comfy.clip_model.clip_preprocess(image, size=518, mean=cv.image_mean, std=cv.image_std, crop=False).float()
dump('pixel_values', pv)
t0 = time.time(); emb = cv.encode_image(image, crop=False)['last_hidden_state'].float(); print('dino %.1f s' % (time.time() - t0), flush=True)
dump('dino_last', emb)

# 2. the DiT at two noise levels, conditional and unconditional
mm.load_model_gpu(model)
dm = model.model.diffusion_model
g = torch.Generator().manual_seed(0); noise = torch.randn(1, 64, 3072, generator=g); dump('noise', noise)
with torch.no_grad():
    for name, sigma, ctx in (('dit_cond_s1', 1.0, emb), ('dit_uncond_s1', 1.0, torch.zeros_like(emb)), ('dit_cond_s05', 0.5, emb)):
        t0 = time.time(); o = dm(noise.float(), torch.tensor([sigma]), ctx.float()); print(name, '%.1f s' % (time.time() - t0), flush=True); dump(name, o)
ms = model.get_model_object('model_sampling'); sig = comfy.samplers.calculate_sigmas(ms, 'normal', 20)
meta['sigmas20'] = [float(s) for s in sig]; print('sigmas', meta['sigmas20'][:4], '...', meta['sigmas20'][-3:])

# 3. the shape VAE: transformer output and a 33^3 grid, from the noise as if it were a latent
fsm = vae.first_stage_model.float()
with torch.no_grad():
    lat = fsm.post_kl(noise.float().movedim(-2, -1)); dump('vae_post_kl', lat)
    lat = fsm.transformer(lat); dump('vae_transformer', lat)
    t0 = time.time(); grid = fsm.decode(noise.float(), num_chunks=8000, octree_resolution=32, enable_pbar=False); print('vae grid %.1f s' % (time.time() - t0), flush=True)
    dump('vae_grid32', grid)
# 4. optional end to end: H3D_STEPS=n runs ComfyUI's own KSampler (euler, normal, cfg 8) from the same noise, then the shape decoder at 64^3
steps = int(os.environ.get('H3D_STEPS', '0'))
if steps:
    import comfy.sample
    pos = [[emb, {}]]; neg = [[torch.zeros_like(emb), {}]]
    t0 = time.time(); lat = comfy.sample.sample(model, noise, steps, 8.0, 'euler', 'normal', pos, neg, torch.zeros_like(noise), seed=0)
    print('sampler %.1f s' % (time.time() - t0), flush=True); dump('e2e_latent', lat); meta['e2e_steps'] = steps
    with torch.no_grad():
        dump('e2e_grid64', fsm.decode(lat.float(), num_chunks=8000, octree_resolution=64, enable_pbar=False))
if steps:
    # the same Euler loop written out by hand in fp32 on the CPU (same formulas as the C++ port), to tell numerics apart from formula errors
    sig = comfy.samplers.calculate_sigmas(ms, 'normal', steps)
    x = noise.clone().float()
    with torch.no_grad():
        for i in range(steps):
            s = float(sig[i]); vc = dm(x, torch.tensor([s]), emb.float()); vu = dm(x, torch.tensor([s]), torch.zeros_like(emb).float())
            x = x + (float(sig[i + 1]) - s) * (vu + 8.0 * (vc - vu))
    dump('manual_latent', x / 0.9990943042622529); meta['sigmas_steps'] = [float(v) for v in sig]
json.dump(meta, open(os.path.join(out, 'meta.json'), 'w')); print('done')

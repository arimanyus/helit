// Copyright 2013 Tom SF Haines

// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at

//   http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.



#include "ms_c.h"

#include <string.h>



// Note to anyone reading this code - the atof function used throughout is not related to the normal function with that name - try not to get confused!
static PyTypeObject MeanShiftType;



void MeanShift_new(MeanShift * this)
{
 this->kernel = &Uniform;
 this->config = NULL; // We know this is valid for the Uniform kernel.
 this->name = NULL; // Only contains a value if it differs from the kernel name.
 this->spatial_type = &KDTreeType;
 this->balls_type = &BallsHashType;
 DataMatrix_init(&this->dm);
 this->weight = -1.0;
 this->norm = -1.0;
 this->spatial = NULL;
 this->balls = NULL;
 this->quality = 0.5;
 this->epsilon = 1e-3;
 this->iter_cap = 1024;
 this->ident_dist = 0.0;
 this->merge_range = 0.5;
 this->merge_check_step = 4;
}

void MeanShift_dealloc(MeanShift * this)
{
 DataMatrix_deinit(&this->dm);
 if (this->spatial!=NULL) Spatial_delete(this->spatial);
 if (this->balls!=NULL) Balls_delete(this->balls);
 this->kernel->config_release(this->config);
}


static PyObject * MeanShift_new_py(PyTypeObject * type, PyObject * args, PyObject * kwds)
{
 // Allocate the object...
  MeanShift * self = (MeanShift*)type->tp_alloc(type, 0);

 // On success construct it...
  if (self!=NULL) MeanShift_new(self);

 // Return the new object...
  return (PyObject*)self;
}

static void MeanShift_dealloc_py(MeanShift * self)
{
 MeanShift_dealloc(self);
 self->ob_type->tp_free((PyObject*)self);
}



static PyObject * MeanShift_kernels_py(MeanShift * self, PyObject * args)
{
 // Create the return list...
  PyObject * ret = PyList_New(0);
  
 // Add each random variable type in turn...
  int i = 0;
  while (ListKernel[i]!=NULL)
  {
   PyObject * name = PyString_FromString(ListKernel[i]->name);
   PyList_Append(ret, name);
   Py_DECREF(name);
   
   ++i; 
  }
 
 // Return...
  return ret;
}


static PyObject * MeanShift_get_kernel_py(MeanShift * self, PyObject * args)
{
 if (self->name==NULL)
 {
  return Py_BuildValue("s", self->kernel->name);
 }
 else
 {
  Py_INCREF(self->name);
  return self->name; 
 }
}


static PyObject * MeanShift_set_kernel_py(MeanShift * self, PyObject * args)
{
 // Parse the parameters...
  char * kname;
  if (!PyArg_ParseTuple(args, "s", &kname)) return NULL;
 
 // Try and find the relevant kernel - if found assign it and return...
  int i = 0;
  while (ListKernel[i]!=NULL)
  {
   int klength = strlen(ListKernel[i]->name);
   if (strncmp(ListKernel[i]->name, kname, klength)==0)
   {
    int dims = DataMatrix_features(&self->dm);
    const char * error = ListKernel[i]->config_verify(dims, kname+klength, NULL);
    if (error!=NULL)
    {
     PyErr_SetString(PyExc_RuntimeError, error);
     return NULL;
    }
    
    self->kernel->config_release(self->config);
    
    self->kernel = ListKernel[i];
    self->config = self->kernel->config_new(dims, kname+klength);
    self->norm = -1.0;
    
    if (self->name!=NULL)
    {
     Py_DECREF(self->name);
     self->name = NULL;
    }
    
    if (self->kernel->configuration!=NULL)
    {
     self->name = Py_BuildValue("s", kname);
    }
    
    Py_INCREF(Py_None);
    return Py_None;
   }
   
   ++i; 
  }
  
 // Was not succesful - throw an error...
  PyErr_SetString(PyExc_RuntimeError, "unrecognised kernel type");
  return NULL; 
}


static PyObject * MeanShift_copy_kernel_py(MeanShift * self, PyObject * args)
{
 // Get the parameters - another mean shift object...
  MeanShift * other;
  if (!PyArg_ParseTuple(args, "O!", &MeanShiftType, &other)) return NULL;
  
 // Clean up current...
  self->kernel->config_release(self->config);
  
  if (self->name!=NULL)
  {
   Py_DECREF(self->name);
   self->name = NULL;
  }
  
 // Copy across...
  self->kernel = other->kernel;
  
  self->config = other->config;
  self->kernel->config_acquire(self->config);
  
  if (other->name!=NULL)
  {
   self->name = other->name;
   Py_INCREF(self->name);
  }
 
 // Return None...
  Py_INCREF(Py_None);
  return Py_None;
}



static PyObject * MeanShift_spatials_py(MeanShift * self, PyObject * args)
{
 // Create the return list...
  PyObject * ret = PyList_New(0);
  
 // Add each spatial type in turn...
  int i = 0;
  while (ListSpatial[i]!=NULL)
  {
   PyObject * name = PyString_FromString(ListSpatial[i]->name);
   PyList_Append(ret, name);
   Py_DECREF(name);
   
   ++i; 
  }
 
 // Return...
  return ret;
}


static PyObject * MeanShift_get_spatial_py(MeanShift * self, PyObject * args)
{
 return Py_BuildValue("s", self->spatial_type->name);
}


static PyObject * MeanShift_set_spatial_py(MeanShift * self, PyObject * args)
{
 // Parse the parameters...
  char * sname;
  if (!PyArg_ParseTuple(args, "s", &sname)) return NULL;
 
 // Try and find the relevant indexing method - if found assign it and return...
  int i = 0;
  while (ListSpatial[i]!=NULL)
  {
   if (strcmp(ListSpatial[i]->name, sname)==0)
   {
    self->spatial_type = ListSpatial[i];
    
    if (self->spatial!=NULL)
    {
     Spatial_delete(self->spatial);
     self->spatial = NULL; 
    }
    
    Py_INCREF(Py_None);
    return Py_None;
   }
   
   ++i; 
  }
  
 // Was not succesful - throw an error...
  PyErr_SetString(PyExc_RuntimeError, "unrecognised spatial type");
  return NULL; 
}



static PyObject * MeanShift_balls_py(MeanShift * self, PyObject * args)
{
 // Create the return list...
  PyObject * ret = PyList_New(0);
  
 // Add each balls type in turn...
  int i = 0;
  while (ListBalls[i]!=NULL)
  {
   PyObject * name = PyString_FromString(ListBalls[i]->name);
   PyList_Append(ret, name);
   Py_DECREF(name);
   
   ++i; 
  }
 
 // Return...
  return ret;
}


static PyObject * MeanShift_get_balls_py(MeanShift * self, PyObject * args)
{
 return Py_BuildValue("s", self->balls_type->name);
}


static PyObject * MeanShift_set_balls_py(MeanShift * self, PyObject * args)
{
 // Parse the parameters...
  char * bname;
  if (!PyArg_ParseTuple(args, "s", &bname)) return NULL;
 
 // Try and find the relevant techneque - if found assign it and return...
  int i = 0;
  while (ListBalls[i]!=NULL)
  {
   if (strcmp(ListBalls[i]->name, bname)==0)
   {
    self->balls_type = ListBalls[i];
   
    // Trash the cluster centers...
     if (self->balls!=NULL)
     {
      Balls_delete(self->balls);
      self->balls = NULL;
     }
  
    Py_INCREF(Py_None);
    return Py_None;
   }
   
   ++i; 
  }
  
 // Was not succesful - throw an error...
  PyErr_SetString(PyExc_RuntimeError, "unrecognised balls type");
  return NULL; 
}



static PyObject * MeanShift_info_py(MeanShift * self, PyObject * args)
{
 // Parse the parameters...
  char * name;
  if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
  int i;
  
 // Try and find the relevant entity - if found assign it and return...
  i = 0;
  while (ListKernel[i]!=NULL)
  {
   if (strcmp(ListKernel[i]->name, name)==0)
   {
    return PyString_FromString(ListKernel[i]->description);
   }
   
   ++i; 
  }
  
  i = 0;
  while (ListSpatial[i]!=NULL)
  {
   if (strcmp(ListSpatial[i]->name, name)==0)
   {
    return PyString_FromString(ListSpatial[i]->description);
   }
   
   ++i; 
  }
  
  i = 0;
  while (ListBalls[i]!=NULL)
  {
   if (strcmp(ListBalls[i]->name, name)==0)
   {
    return PyString_FromString(ListBalls[i]->description);
   }
   
   ++i; 
  }
  
 // Was not succesful - throw an error...
  PyErr_SetString(PyExc_RuntimeError, "unrecognised entity name");
  return NULL; 
}


static PyObject * MeanShift_info_config_py(MeanShift * self, PyObject * args)
{
 // Parse the parameters...
  char * name;
  if (!PyArg_ParseTuple(args, "s", &name)) return NULL;
  int i;
  
 // Try and find the relevant entity - if found assign it and return...
  i = 0;
  while (ListKernel[i]!=NULL)
  {
   if (strcmp(ListKernel[i]->name, name)==0)
   {
    if (ListKernel[i]->configuration!=NULL)
    {
     return PyString_FromString(ListKernel[i]->configuration);
    }
    else
    {
     Py_INCREF(Py_None);
     return Py_None;
    }
   }
   
   ++i; 
  }

 // Was not succesful - throw an error...
  PyErr_SetString(PyExc_RuntimeError, "unrecognised kernel name");
  return NULL; 
}



static PyObject * MeanShift_set_data_py(MeanShift * self, PyObject * args)
{
 // Extract the parameters...
  PyArrayObject * data;
  char * dim_types;
  PyObject * weight_index = NULL;
  if (!PyArg_ParseTuple(args, "O!s|O", &PyArray_Type, &data, &dim_types, &weight_index)) return NULL;
  
 // Check its all ok...
  if (strlen(dim_types)!=PyArray_NDIM(data))
  {
   PyErr_SetString(PyExc_RuntimeError, "dimension type string must be the same length as the number of dimensions in the data matrix");
   return NULL;
  }
  
  if ((PyArray_DESCR(data)->kind!='b')&&(PyArray_DESCR(data)->kind!='i')&&(PyArray_DESCR(data)->kind!='u')&&(PyArray_DESCR(data)->kind!='f'))
  {
   PyErr_SetString(PyExc_RuntimeError, "provided data matrix is not of a supported type");
   return NULL; 
  }
  
  int i;
  for (i=0; i<PyArray_NDIM(data); i++)
  {
   if ((dim_types[i]!='d')&&(dim_types[i]!='f')&&(dim_types[i]!='b'))
   {
    PyErr_SetString(PyExc_RuntimeError, "dimension type string includes an unrecognised code"); 
   }
  }
 
 // Handle the weight index...
  int weight_i = -1;
  if ((weight_index!=NULL)&&(weight_index!=Py_None))
  {
   if (PyInt_Check(weight_index)==0)
   {
    PyErr_SetString(PyExc_RuntimeError, "weight index must be an integer");
    return NULL;  
   }
    
   weight_i = PyInt_AsLong(weight_index);
  }
  
 // Make the assignment...
  DimType * dt = (DimType*)malloc(PyArray_NDIM(data) * sizeof(DimType));
  for (i=0; i<PyArray_NDIM(data); i++)
  {
   switch (dim_types[i])
   {
    case 'd': dt[i] = DIM_DATA;    break;
    case 'f': dt[i] = DIM_FEATURE; break;
    case 'b': dt[i] = DIM_DUAL;    break;
   }
  }
  
  DataMatrix_set(&self->dm, data, dt, weight_i);
  free(dt);
  
 // Trash the spatial...
  if (self->spatial!=NULL)
  {
   Spatial_delete(self->spatial);
   self->spatial = NULL; 
  }
  
 // Trash the cluster centers...
  if (self->balls!=NULL)
  {
   Balls_delete(self->balls);
   self->balls = NULL;
  }
  
 // Trash the weight record...
  self->weight = -1.0;
  self->norm = -1.0;
 
 // Return None...
  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject * MeanShift_get_dm_py(MeanShift * self, PyObject * args)
{
 Py_INCREF((PyObject*)self->dm.array);
 return (PyObject*)self->dm.array;
}


static PyObject * MeanShift_get_dim_py(MeanShift * self, PyObject * args)
{
 PyObject * ret = PyString_FromStringAndSize(NULL, PyArray_NDIM(self->dm.array));
 char * out = PyString_AsString(ret);
 
 int i;
 for (i=0;i<PyArray_NDIM(self->dm.array);i++)
 {
  switch (self->dm.dt[i])
  {
   case DIM_DATA:    out[i] = 'd'; break;
   case DIM_FEATURE: out[i] = 'f'; break;
   case DIM_DUAL:    out[i] = 'b'; break;
   default: out[i] = 'e'; break; // Should never happen, obviously.
  }
 }
  
 return ret;
}


static PyObject * MeanShift_get_weight_dim_py(MeanShift * self, PyObject * args)
{
 if (self->dm.weight_index>=0)
 {
  return Py_BuildValue("f", self->dm.weight_index);
 }
 else
 {
  Py_INCREF(Py_None);
  return Py_None;
 }
}



static PyObject * MeanShift_set_scale_py(MeanShift * self, PyObject * args)
{
 // Extract the parameters...
  PyArrayObject * scale;
  float weight_scale = 1.0;
  if (!PyArg_ParseTuple(args, "O!|f", &PyArray_Type, &scale, &weight_scale)) return NULL;
 
 // Handle the scale...
  if ((PyArray_NDIM(scale)!=1)||(PyArray_DIMS(scale)[0]!=DataMatrix_features(&self->dm)))
  {
   PyErr_SetString(PyExc_RuntimeError, "scale vector must be a simple 1D numpy array with length matching the number of features.");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(scale));
  
  float * s = (float*)malloc(PyArray_DIMS(scale)[0] * sizeof(float));
  int i;
  for (i=0; i<PyArray_DIMS(scale)[0]; i++)
  {
   s[i] = atof(PyArray_GETPTR1(scale, i));
  }
  
  DataMatrix_set_scale(&self->dm, s, weight_scale);
  free(s);
  
 // Trash the spatial - changing either of the above invalidates it...
  if (self->spatial!=NULL)
  {
   Spatial_delete(self->spatial);
   self->spatial = NULL; 
  }
  
 // Trash the cluster centers...
  if (self->balls!=NULL)
  {
   Balls_delete(self->balls);
   self->balls = NULL;
  }
  
 // Trash the weight record...
  self->weight = -1.0;
  self->norm = -1.0;
 
 // Return None...
  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject * MeanShift_get_scale_py(MeanShift * self, PyObject * args)
{
 npy_intp dim = DataMatrix_features(&self->dm);
 PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(1, &dim, NPY_FLOAT32);
 
 int i;
 for (i=0; i<dim; i++)
 {
  *(float*)PyArray_GETPTR1(ret, i) = self->dm.mult[i];
 }
 
 return (PyObject*)ret;
}


static PyObject * MeanShift_get_weight_scale_py(MeanShift * self, PyObject * args)
{
 return Py_BuildValue("f", self->dm.weight_scale);
}



static PyObject * MeanShift_exemplars_py(MeanShift * self, PyObject * args)
{
 return Py_BuildValue("i", DataMatrix_exemplars(&self->dm));
}


static PyObject * MeanShift_features_py(MeanShift * self, PyObject * args)
{
 return Py_BuildValue("i", DataMatrix_features(&self->dm));
}


float MeanShift_weight(MeanShift * this)
{
 if (this->weight<0.0)
 {
  this->weight = calc_weight(&this->dm);
 }
  
 return this->weight;
}


static PyObject * MeanShift_weight_py(MeanShift * self, PyObject * args)
{
 return Py_BuildValue("f", MeanShift_weight(self));
}


static PyObject * MeanShift_stats_py(MeanShift * self, PyObject * args)
{
 // Prep...
  int exemplars = DataMatrix_exemplars(&self->dm);
  npy_intp features = DataMatrix_features(&self->dm);
  
  PyArrayObject * mean = (PyArrayObject*)PyArray_SimpleNew(1, &features, NPY_FLOAT32);
  PyArrayObject * sd   = (PyArrayObject*)PyArray_SimpleNew(1, &features, NPY_FLOAT32);
  
  int i, j;
  for (j=0; j<features; j++)
  {
   *(float*)PyArray_GETPTR1(mean, j) = 0.0;
   *(float*)PyArray_GETPTR1(sd, j)   = 0.0;
  }
  
 // Single pass to calculate everything at once...
  float total = 0.0;  
  for (i=0; i<exemplars; i++)
  {
   float w;
   float * fv = DataMatrix_fv(&self->dm, i, &w);
   float new_total = total + w;
   
   for (j=0; j<features; j++)
   {
    float delta = fv[j] - *(float*)PyArray_GETPTR1(mean , j);
    float r = delta * w / new_total;
    *(float*)PyArray_GETPTR1(mean, j) += r;
    *(float*)PyArray_GETPTR1(sd, j) += total * delta * r;
   }
    
   total = new_total;
  }
    
 // Finish the sd and adapt the scale...
  if (total<1e-6) total = 1e-6; // Safety, for if they are all outliers.
  for (j=0; j<features; j++)
  {
   *(float*)PyArray_GETPTR1(mean, j) /= self->dm.mult[j];
   *(float*)PyArray_GETPTR1(sd, j) = sqrt(*(float*)PyArray_GETPTR1(sd, j) / total) / self->dm.mult[j];
  }
  
 // Construct and do the return...
  return Py_BuildValue("(N,N)", mean, sd);
}



static PyObject * MeanShift_scale_silverman_py(MeanShift * self, PyObject * args)
{
 int i, j;
 
 // Reset the scale to 1 before we start, so fv does something sensible...
  int exemplars = DataMatrix_exemplars(&self->dm);
  int features = DataMatrix_features(&self->dm);
  
  for (i=0; i<features; i++) self->dm.mult[i] = 1.0;
  
 // Use Silverman's rule of thumb to calculate scale values...
  float weight = 0.0;
  float * mean = (float*)malloc(features * sizeof(float));
  float * sd = (float*)malloc(features * sizeof(float));
  
  // Calculate and store the standard deviation of each dimension...
   for (i=0; i<features; i++)
   {
    mean[i] = 0.0;
    sd[i] = 0.0;
   }
   
   for (j=0; j<exemplars; j++)
   {
    float w;
    float * fv = DataMatrix_fv(&self->dm, j, &w);
    float temp = weight + w;
    
    for (i=0; i<features; i++)
    {
     float delta = fv[i] - mean[i];
     float r = delta * w / temp;
     mean[i] += r;
     sd[i] += weight * delta * r;
    }
    
    weight = temp;
   }
   
   for (i=0; i<features; i++)
   {
    sd[i] = sqrt(sd[i] / weight);
   }
  
  // Convert standard deviations into a bandwidth via applying the rule...
   float mult = pow(weight * (features + 2.0) / 4.0, -1.0 / (features + 4.0));
   for (i=0; i<features; i++)
   {
    sd[i] = 1.0 / (sd[i] * mult);
   }
 
 // Set the scale...
  DataMatrix_set_scale(&self->dm, sd, self->dm.weight_scale);
  free(sd);
  free(mean);
 
 // Trash the spatial - changing the above invalidates it...
  if (self->spatial!=NULL)
  {
   Spatial_delete(self->spatial);
   self->spatial = NULL; 
  }
  
 // Trash the cluster centers...
  if (self->balls!=NULL)
  {
   Balls_delete(self->balls);
   self->balls = NULL;
  }
  
 // Trash the normalising constant...
  self->norm = -1.0;

 // Return None...
  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject * MeanShift_scale_scott_py(MeanShift * self, PyObject * args)
{
 int i, j;
 
 // Reset the scale to 1 before we start, so fv does something sensible...
  int exemplars = DataMatrix_exemplars(&self->dm);
  int features = DataMatrix_features(&self->dm);
  
  for (i=0; i<features; i++) self->dm.mult[i] = 1.0;
 
 // Use Silverman's rule fo thumb to calculate scale values...   
  float weight = 0.0;
  float * mean = (float*)malloc(features * sizeof(float));
  float * sd = (float*)malloc(features * sizeof(float));
  
  // Calculate and store into s the standard deviation of each dimension...
   for (i=0; i<features; i++)
   {
    mean[i] = 0.0;
    sd[i] = 0.0;
   }
   
   for (j=0; j<exemplars; j++)
   {
    float w;
    float * fv = DataMatrix_fv(&self->dm, j, &w);
    float temp = weight + w;
    
    for (i=0; i<features; i++)
    {
     float delta = fv[i] - mean[i];
     float r = delta * w / temp;
     mean[i] += r;
     sd[i] += weight * delta * r;
    }
    
    weight = temp;
   }
   
   for (i=0; i<features; i++)
   {
    sd[i] = sqrt(sd[i] / weight);
   }
  
  // Convert standard deviations into a bandwidth via applying the rule...
   float mult = pow(weight, -1.0 / (features + 4.0));
   for (i=0; i<features; i++)
   {
    sd[i] = 1.0 / (sd[i] * mult);
   }
 
 // Set the scale...
  DataMatrix_set_scale(&self->dm, sd, self->dm.weight_scale);
  free(sd);
  free(mean);
 
 // Trash the spatial - changing the above invalidates it...
  if (self->spatial!=NULL)
  {
   Spatial_delete(self->spatial);
   self->spatial = NULL; 
  }
  
 // Trash the cluster centers...
  if (self->balls!=NULL)
  {
   Balls_delete(self->balls);
   self->balls = NULL;
  }
  
 // Trash the normalising constant...
  self->norm = -1.0;
  
 // Return None...
  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject * MeanShift_loo_nll_py(MeanShift * self, PyObject * args)
{
 // Extract the limit from the parameters...
  float limit = 1e-16;
  if (!PyArg_ParseTuple(args, "|f", &limit)) return NULL;
 
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Calculate the normalising term if needed...
  if (self->norm<0.0)
  {
   self->norm = calc_norm(&self->dm, self->kernel, self->config, MeanShift_weight(self));
  }
  
 // Calculate the probability...
  float nll = loo_nll(self->spatial, self->kernel, self->config, self->norm, self->quality, limit);
 
 // Return it...
  return Py_BuildValue("f", nll);
}



static PyObject * MeanShift_prob_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a feature vector... 
  PyArrayObject * start;
  if (!PyArg_ParseTuple(args, "O!", &PyArray_Type, &start)) return NULL;
  
 // Check the input is acceptable...
  npy_intp feats = DataMatrix_features(&self->dm);
  if ((PyArray_NDIM(start)!=1)||(PyArray_DIMS(start)[0]!=feats))
  {
   PyErr_SetString(PyExc_RuntimeError, "input vector must be 1D with the same length as the number of features.");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Calculate the normalising term if needed...
  if (self->norm<0.0)
  {
   self->norm = calc_norm(&self->dm, self->kernel, self->config, MeanShift_weight(self));
  }
 
 // Create a temporary to hold the feature vector...
  float * fv = (float*)malloc(feats * sizeof(float));
  int i;
  for (i=0; i<feats; i++)
  {
   fv[i] = atof(PyArray_GETPTR1(start, i)) * self->dm.mult[i];
  }
  
 // Calculate the probability...
  float p = prob(self->spatial, self->kernel, self->config, fv, self->norm, self->quality);
 
 // Return the calculated probability...
  return Py_BuildValue("f", p);
}



static PyObject * MeanShift_probs_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a data matrix... 
  PyArrayObject * start;
  if (!PyArg_ParseTuple(args, "O!", &PyArray_Type, &start)) return NULL;

 // Check the input is acceptable...
  npy_intp feats = DataMatrix_features(&self->dm);
  if ((PyArray_NDIM(start)!=2)||(PyArray_DIMS(start)[1]!=feats))
  {
   PyErr_SetString(PyExc_RuntimeError, "input matrix must be 2D with the same length as the number of features in the second dimension");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));

 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Calculate the normalising term if needed...
  if (self->norm<0.0)
  {
   self->norm = calc_norm(&self->dm, self->kernel, self->config, MeanShift_weight(self));
  }
  
 // Create a temporary array of floats...
  float * fv = (float*)malloc(feats * sizeof(float));

 // Create the output array... 
  PyArrayObject * out = (PyArrayObject*)PyArray_SimpleNew(1, PyArray_DIMS(start), NPY_FLOAT32);
  
  
 // Run the algorithm...
  int i;
  for (i=0; i<PyArray_DIMS(start)[0]; i++)
  {
   // Copy the feature vector into the temporary storage...
    int j;
    for (j=0; j<feats; j++)
    {
     fv[j] = atof(PyArray_GETPTR2(start, i, j)) * self->dm.mult[j];
    }
   
   // Calculate the probability...
    float p = prob(self->spatial, self->kernel, self->config, fv, self->norm, self->quality);
   
   // Store it...
    *(float*)PyArray_GETPTR1(out, i) = p;
  }
  
 // Clean up...
  free(fv);
 
 // Return the assigned clusters...
  return (PyObject*)out;
}



static PyObject * MeanShift_draw_py(MeanShift * self, PyObject * args)
{
 // Get the arguments - indices for the rng... 
  unsigned int index[3] = {0, 0, 0};
  if (!PyArg_ParseTuple(args, "I|II", &index[0], &index[1], &index[2])) return NULL;
  
 // Create the return array...
  npy_intp feats = DataMatrix_features(&self->dm);
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(1, &feats, NPY_FLOAT32);
  
 // Generate the return...
  draw(&self->dm, self->kernel, self->config, index, (float*)PyArray_DATA(ret));
  
 // Return the draw...
  return (PyObject*)ret;
}



static PyObject * MeanShift_draws_py(MeanShift * self, PyObject * args)
{
 // Get the arguments - how many to output and indices for the rng... 
  npy_intp shape[2];
  unsigned int index[3] = {0, 0, 0};
  if (!PyArg_ParseTuple(args, "iI|I", &shape[0], &index[0], &index[1])) return NULL;
  
 // Create the return array...
  shape[1] = DataMatrix_features(&self->dm);
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(2, shape, NPY_FLOAT32);
  
 // Fill in the return matrix...
  for (index[2]=0; index[2]<shape[0]; index[2]++)
  {
   float * out = (float*)PyArray_GETPTR2(ret, index[2], 0);
   draw(&self->dm, self->kernel, self->config, index, out);
  }
  
 // Return the draw...
  return (PyObject*)ret;
}



static PyObject * MeanShift_bootstrap_py(MeanShift * self, PyObject * args)
{
 // Get the arguments - how many to output and indices for the rng... 
  npy_intp shape[2];
  unsigned int index[4] = {0, 0, 0, 0};
  if (!PyArg_ParseTuple(args, "iI|II", &shape[0], &index[0], &index[1], &index[2])) return NULL;
  
 // Create the return array...
  shape[1] = DataMatrix_features(&self->dm);
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(2, shape, NPY_FLOAT32);
  
 // Fill in the return matrix...
  for (index[3]=0; index[3]<shape[0]; index[3]++)
  {
   int ind = DataMatrix_draw(&self->dm, index);
   float * fv = DataMatrix_fv(&self->dm, ind, NULL);
   
   int i;
   for (i=0; i<shape[1]; i++)
   {
    *(float*)PyArray_GETPTR2(ret, index[3], i) = fv[i] / self->dm.mult[i];
   }
  }
  
 // Return the draw...
  return (PyObject*)ret;
}



static PyObject * MeanShift_mode_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a feature vector... 
  PyArrayObject * start;
  if (!PyArg_ParseTuple(args, "O!", &PyArray_Type, &start)) return NULL;
  
 // Check the input is acceptable...
  npy_intp feats = DataMatrix_features(&self->dm);
  if ((PyArray_NDIM(start)!=1)||(PyArray_DIMS(start)[0]!=feats))
  {
   PyErr_SetString(PyExc_RuntimeError, "input vector must be 1D with the same length as the number of features.");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // Create an output matrix, copy in the data, applying the scale change...  
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(1, &feats, NPY_FLOAT32);
  
  int i;
  for (i=0; i<feats; i++)
  {
   float * out = (float*)PyArray_GETPTR1(ret, i);
   *out = atof(PyArray_GETPTR1(start, i)) * self->dm.mult[i];
  }
 
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Run the agorithm; we need some temporary storage...
  float * temp = (float*)malloc(feats * sizeof(float));
  mode(self->spatial, self->kernel, self->config, (float*)PyArray_DATA(ret), temp, self->quality, self->epsilon, self->iter_cap);
  free(temp);
   // Undo the scale change...
  for (i=0; i<feats; i++)
  {
   float * out = (float*)PyArray_GETPTR1(ret, i);
   *out /= self->dm.mult[i];
  }
 
 // Return the array...
  return (PyObject*)ret;
}



static PyObject * MeanShift_modes_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a data matrix... 
  PyArrayObject * start;
  if (!PyArg_ParseTuple(args, "O!", &PyArray_Type, &start)) return NULL;

 // Check the input is acceptable...
  npy_intp dims[2];
  dims[0] = PyArray_DIMS(start)[0];
  dims[1] = DataMatrix_features(&self->dm);
  
  if ((PyArray_NDIM(start)!=2)||(PyArray_DIMS(start)[1]!=dims[1]))
  {
   PyErr_SetString(PyExc_RuntimeError, "input matrix must be 2D with the same length as the number of features in the second dimension");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // Create an output matrix, copy in the data, applying the scale change...  
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  
  int i,j;
  for (i=0; i<dims[0]; i++)
  {
   for (j=0; j<dims[1]; j++)
   {
    *(float*)PyArray_GETPTR2(ret, i, j) = atof(PyArray_GETPTR2(start, i, j)) * self->dm.mult[j];
   }
  }
  
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Calculate each mode in turn, including undo any scale changes...
  float * temp = (float*)malloc(dims[1] * sizeof(float));
  for (i=0; i<dims[0]; i++)
  {
   float * out = (float*)PyArray_GETPTR1(ret, i);
   
   mode(self->spatial, self->kernel, self->config, out, temp, self->quality, self->epsilon, self->iter_cap);
   
   for (j=0; j<dims[1]; j++) out[j] /= self->dm.mult[j];
  }
  free(temp); 
  
 // Return the matrix of modes...
  return (PyObject*)ret;
}



static PyObject * MeanShift_modes_data_py(MeanShift * self, PyObject * args)
{
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }

 // Work out the output matrix size...
  int nd = 1;
  int i;
  for (i=0; i<PyArray_NDIM(self->dm.array); i++)
  {
   if (self->dm.dt[i]!=DIM_FEATURE) nd += 1;
  }
  
  npy_intp * dims = (npy_intp*)malloc(nd * sizeof(npy_intp));
  
  nd = 0;
  for (i=0; i<PyArray_NDIM(self->dm.array); i++)
  {
   if (self->dm.dt[i]!=DIM_FEATURE)
   {
    dims[nd] = PyArray_DIMS(self->dm.array)[i];
    nd += 1;
   }
  }
  
  dims[nd] = DataMatrix_features(&self->dm);
  nd += 1;
 
 // Create the output matrix...
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(nd, dims, NPY_FLOAT32);
 
 // Iterate and do each entry in turn...
  float * temp = (float*)malloc(dims[nd-1] * sizeof(float));
  
  float * out = (float*)PyArray_DATA(ret);
  int loc = 0;
  while (loc<DataMatrix_exemplars(&self->dm))
  {
   // Copy in the relevent feature vector...
    float * fv = DataMatrix_fv(&self->dm, loc, NULL);
    for (i=0; i<dims[nd-1]; i++) out[i] = fv[i];
   
   // Converge mean shift...
    mode(self->spatial, self->kernel, self->config, out, temp, self->quality, self->epsilon, self->iter_cap);
    
   // Undo any scale change...
    for (i=0; i<dims[nd-1]; i++) out[i] /= self->dm.mult[i];
    
   // Move to next position, detecting when we are done... 
    loc += 1;
    out += dims[nd-1];
  }
 
 // Clean up...
  free(temp);
  free(dims);
 
 // Return...
  return (PyObject*)ret;
}



static PyObject * MeanShift_cluster_py(MeanShift * self, PyObject * args)
{
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }

 // Work out the output matrix size...
  int nd = 0;
  int i;
  for (i=0; i<PyArray_NDIM(self->dm.array); i++)
  {
   if (self->dm.dt[i]!=DIM_FEATURE) nd += 1;
  }
  
  if (nd<2) nd = 2; // So the array can be abused below
  npy_intp * dims = (npy_intp*)malloc(nd * sizeof(npy_intp));
  
  nd = 0;
  for (i=0; i<PyArray_NDIM(self->dm.array); i++)
  {
   if (self->dm.dt[i]!=DIM_FEATURE)
   {
    dims[nd] = PyArray_DIMS(self->dm.array)[i];
    nd += 1;
   }
  }
  
 // Create the output matrix...
  PyArrayObject * index = (PyArrayObject*)PyArray_SimpleNew(nd, dims, NPY_INT32);
 
 // Create the balls...
  if (self->balls!=NULL) Balls_delete(self->balls);
  self->balls = Balls_new(self->balls_type, self->dm.feats, self->merge_range);
 
 // Do the work...
  cluster(self->spatial, self->kernel, self->config, self->balls, (int*)PyArray_DATA(index), self->quality, self->epsilon, self->iter_cap, self->ident_dist, self->merge_range, self->merge_check_step);
 
 // Extract the modes, which happen to be the centers of the balls...
  dims[0] = Balls_count(self->balls);
  dims[1] = Balls_dims(self->balls);
  
  PyArrayObject * modes = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  
  for (i=0; i<dims[0]; i++)
  {
   const float * loc = Balls_pos(self->balls, i);
   
   int j;
   for (j=0; j<dims[1]; j++)
   {
    *(float*)PyArray_GETPTR2(modes, i, j) = loc[j] / self->dm.mult[j]; 
   }
  }
 
 // Clean up...
  free(dims);
 
 // Return the tuple of (modes, assignment)...
  return Py_BuildValue("(N,N)", modes, index);
}



static PyObject * MeanShift_assign_cluster_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a feature vector... 
  PyArrayObject * start;
  if (!PyArg_ParseTuple(args, "O!", &PyArray_Type, &start)) return NULL;
  
 // Check the input is acceptable...
  npy_intp feats = DataMatrix_features(&self->dm);
  if ((PyArray_NDIM(start)!=1)||(PyArray_DIMS(start)[0]!=feats))
  {
   PyErr_SetString(PyExc_RuntimeError, "input vector must be 1D with the same length as the number of features.");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // Verify that cluster has been run...
  if (self->balls==NULL)
  {
   PyErr_SetString(PyExc_RuntimeError, "the cluster method must be run before the assign_cluster method.");
   return NULL; 
  }
  
 // Create two temporary array fo floats, putting the feature vector into one of them...
  float * fv = (float*)malloc(feats * sizeof(float));
  float * temp = (float*)malloc(feats * sizeof(float));

  int i;
  for (i=0; i<feats; i++)
  {
   fv[i] = atof(PyArray_GETPTR1(start, i)) * self->dm.mult[i];
  }
 
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Run the algorithm...
  int cluster = assign_cluster(self->spatial, self->kernel, self->config, self->balls, fv, temp, self->quality, self->epsilon, self->iter_cap, self->merge_check_step);
  
 // Clean up...
  free(temp);
  free(fv);
 
 // Return the assigned cluster...
  return Py_BuildValue("i", cluster);
}



static PyObject * MeanShift_assign_clusters_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a feature vector... 
  PyArrayObject * start;
  if (!PyArg_ParseTuple(args, "O!", &PyArray_Type, &start)) return NULL;
  
 // Check the input is acceptable...
  npy_intp feats = DataMatrix_features(&self->dm);
  if ((PyArray_NDIM(start)!=2)||(PyArray_DIMS(start)[1]!=feats))
  {
   PyErr_SetString(PyExc_RuntimeError, "input vector must be 2D with the second dimension the same length as the number of features.");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // Verify that cluster has been run...
  if (self->balls==NULL)
  {
   PyErr_SetString(PyExc_RuntimeError, "the cluster method must be run before the assign_cluster method.");
   return NULL; 
  }
  
 // Create two temporary array of floats...
  float * fv = (float*)malloc(feats * sizeof(float));
  float * temp = (float*)malloc(feats * sizeof(float));

 // Create the output array... 
  PyArrayObject * cluster = (PyArrayObject*)PyArray_SimpleNew(1, PyArray_DIMS(start), NPY_INT32);
 
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Run the algorithm...
  int i;
  for (i=0; i<PyArray_DIMS(start)[0]; i++)
  {
   // Copy the feature vector into the temporary storage...
    int j;
    for (j=0; j<feats; j++)
    {
     fv[j] = atof(PyArray_GETPTR2(start, i, j)) * self->dm.mult[j];
    }
   
   // Run it...
    int c = assign_cluster(self->spatial, self->kernel, self->config, self->balls, fv, temp, self->quality, self->epsilon, self->iter_cap, self->merge_check_step);
   
   // Store the result...
    *(int*)PyArray_GETPTR1(cluster, i) = c;
  }
  
 // Clean up...
  free(temp);
  free(fv);
 
 // Return the assigned clusters...
  return (PyObject*)cluster;
}



static PyObject * MeanShift_manifold_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a feature vector and degrees of freedom for the manifold... 
  PyArrayObject * start;
  int degrees;
  PyObject * always_hessian = Py_True;
  if (!PyArg_ParseTuple(args, "O!i|O", &PyArray_Type, &start, &degrees, &always_hessian)) return NULL;
  
  if (PyBool_Check(always_hessian)==0)
  {
   PyErr_SetString(PyExc_RuntimeError, "Parameter indicating if to calculate the hessian for every step or not should be boolean");
   return NULL;  
  }

 // Check the input is acceptable...
  npy_intp feats = DataMatrix_features(&self->dm);
  if ((PyArray_NDIM(start)!=1)||(PyArray_DIMS(start)[0]!=feats))
  {
   PyErr_SetString(PyExc_RuntimeError, "input vector must be 1D with the same length as the number of features.");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // Create an output matrix, copy in the data, applying the scale change...  
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(1, &feats, NPY_FLOAT32);
  
  int i;
  for (i=0; i<feats; i++)
  {
   float * out = (float*)PyArray_GETPTR1(ret, i);
   *out = atof(PyArray_GETPTR1(start, i)) * self->dm.mult[i];
  }
 
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Run the agorithm; we need some temporary storage...
  float * grad = (float*)malloc(feats * sizeof(float));
  float * hess = (float*)malloc(feats * feats * sizeof(float));
  float * eigen_vec = (float*)malloc(feats * feats * sizeof(float));
  float * eigen_val = (float*)malloc(feats * sizeof(float));
  
  manifold(self->spatial, degrees, (float*)PyArray_DATA(ret), grad, hess, eigen_val, eigen_vec, self->quality, self->epsilon, self->iter_cap, (always_hessian==Py_False) ? 0 : 1);
  
  free(eigen_val);
  free(eigen_vec);
  free(hess);
  free(grad);
  
 // Undo the scale change...
  for (i=0; i<feats; i++)
  {
   float * out = (float*)PyArray_GETPTR1(ret, i);
   *out /= self->dm.mult[i];
  }
 
 // Return the array...
  return (PyObject*)ret;
}



static PyObject * MeanShift_manifolds_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a data matrix and degrees of freedom for the manifold... 
  PyArrayObject * start;
  int degrees;
  PyObject * always_hessian = Py_True;
  if (!PyArg_ParseTuple(args, "O!i|O", &PyArray_Type, &start, &degrees, &always_hessian)) return NULL;

  if (PyBool_Check(always_hessian)==0)
  {
   PyErr_SetString(PyExc_RuntimeError, "Parameter indicating if to calculate the hessian for every step or not should be boolean");
   return NULL;  
  }
  
 // Check the input is acceptable...
  npy_intp dims[2];
  dims[0] = PyArray_DIMS(start)[0];
  dims[1] = DataMatrix_features(&self->dm);
  
  if ((PyArray_NDIM(start)!=2)||(PyArray_DIMS(start)[1]!=dims[1]))
  {
   PyErr_SetString(PyExc_RuntimeError, "input matrix must be 2D with the same length as the number of features in the second dimension");
   return NULL;
  }
  ToFloat atof = KindToFunc(PyArray_DESCR(start));
  
 // Create an output matrix, copy in the data, applying the scale change...  
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(2, dims, NPY_FLOAT32);
  
  int i,j;
  for (i=0; i<dims[0]; i++)
  {
   for (j=0; j<dims[1]; j++)
   {
    *(float*)PyArray_GETPTR2(ret, i, j) = atof(PyArray_GETPTR2(start, i, j)) * self->dm.mult[j];
   }
  }
  
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }
  
 // Calculate each mode in turn, including undo any scale changes...
  float * grad = (float*)malloc(dims[1] * sizeof(float));
  float * hess = (float*)malloc(dims[1] * dims[1] * sizeof(float));
  float * eigen_vec = (float*)malloc(dims[1] * dims[1] * sizeof(float));
  float * eigen_val = (float*)malloc(dims[1] * sizeof(float));
  
  for (i=0; i<dims[0]; i++)
  {
   float * out = (float*)PyArray_GETPTR1(ret,i);
   
   manifold(self->spatial, degrees, out, grad, hess, eigen_val, eigen_vec, self->quality, self->epsilon, self->iter_cap, (always_hessian==Py_False) ? 0 : 1);
   
   for (j=0; j<dims[1]; j++) out[j] /= self->dm.mult[j];
  }
  
  free(eigen_val);
  free(eigen_vec);
  free(hess);
  free(grad);
  
 // Return the matrix of modes...
  return (PyObject*)ret;
}



static PyObject * MeanShift_manifolds_data_py(MeanShift * self, PyObject * args)
{
 // Get the argument - a data matrix... 
  int degrees;
  PyObject * always_hessian = Py_True;
  if (!PyArg_ParseTuple(args, "i|O", &degrees, &always_hessian)) return NULL;
 
  if (PyBool_Check(always_hessian)==0)
  {
   PyErr_SetString(PyExc_RuntimeError, "Parameter indicating if to calculate the hessian for every step or not should be boolean");
   return NULL;  
  }
  
 // If spatial is null create it...
  if (self->spatial==NULL)
  {
   self->spatial = Spatial_new(self->spatial_type, &self->dm); 
  }

 // Work out the output matrix size...
  int nd = 1;
  int i;
  for (i=0; i<PyArray_NDIM(self->dm.array); i++)
  {
   if (self->dm.dt[i]!=DIM_FEATURE) nd += 1;
  }
  
  npy_intp * dims = (npy_intp*)malloc(nd * sizeof(npy_intp));
  
  nd = 0;
  for (i=0; i<PyArray_NDIM(self->dm.array); i++)
  {
   if (self->dm.dt[i]!=DIM_FEATURE)
   {
    dims[nd] = PyArray_DIMS(self->dm.array)[i];
    nd += 1;
   }
  }
  
  dims[nd] = DataMatrix_features(&self->dm);
  nd += 1;
 
 // Create the output matrix...
  PyArrayObject * ret = (PyArrayObject*)PyArray_SimpleNew(nd, dims, NPY_FLOAT32);
 
 // Iterate and do each entry in turn...
  float * grad = (float*)malloc(dims[1] * sizeof(float));
  float * hess = (float*)malloc(dims[1] * dims[1] * sizeof(float));
  float * eigen_vec = (float*)malloc(dims[1] * dims[1] * sizeof(float));
  float * eigen_val = (float*)malloc(dims[1] * sizeof(float));
  
  float * out = (float*)PyArray_DATA(ret);
  int loc = 0;
  while (loc<DataMatrix_exemplars(&self->dm))
  {
   // Copy in the relevent feature vector...
    float * fv = DataMatrix_fv(&self->dm, loc, NULL);
    for (i=0; i<dims[nd-1]; i++) out[i] = fv[i];
   
   // Converge mean shift...
    manifold(self->spatial, degrees, out, grad, hess, eigen_val, eigen_vec, self->quality, self->epsilon, self->iter_cap, (always_hessian==Py_False) ? 0 : 1);
    
   // Undo any scale change...
    for (i=0; i<dims[nd-1]; i++) out[i] /= self->dm.mult[i];

   // Move to next position, detecting when we are done... 
    loc += 1;
    out += dims[nd-1];
  }
 
 // Clean up...
  free(eigen_val);
  free(eigen_vec);
  free(hess);
  free(grad);
  free(dims);
 
 // Return...
  return (PyObject*)ret;
}



static PyObject * MeanShift_mult_py(MeanShift * self, PyObject * args, PyObject * kw)
{
 // Handle the parameters...
  PyObject * multiplicands;
  PyArrayObject * output;
  
  unsigned int rng0 = 0;
  unsigned int rng1 = 0;
  int gibbs = 16;
  int mci = 64;
  int mh = 8;
  int fake = 0;
  
  static char * kw_list[] = {"multiplicands", "output", "rng0", "rng1", "gibbs", "mci", "mh", "fake", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kw, "O!O!|IIiiii", kw_list, &PyList_Type, &multiplicands, &PyArray_Type, &output, &rng0, &rng1, &gibbs, &mci, &mh, &fake)) return NULL;
  
 // Verify the parameters are all good...
  int terms = PyList_Size(multiplicands);
  if (terms<1)
  {
   PyErr_SetString(PyExc_RuntimeError, "Need some MeanShift objects to multiply");
   return NULL;
  }
  
  if (PyObject_IsInstance(PyList_GetItem(multiplicands, 0), (PyObject*)&MeanShiftType)!=1)
  {
   PyErr_SetString(PyExc_RuntimeError, "First item in multiplicand list is not a MeanShift object");
   return NULL; 
  }
  
  self = (MeanShift*)PyList_GetItem(multiplicands, 0); // Bit weird, but why not? - self is avaliable and free to dance!
  int dims = DataMatrix_features(&self->dm);
  
  int longest = DataMatrix_exemplars(&self->dm);
  if (longest==0)
  {
   PyErr_SetString(PyExc_RuntimeError, "First item in multiplicand list has no exemplars in its KDE");
   return NULL;
  }
  
  int i;
  for (i=1; i<terms; i++)
  {
   PyObject * temp = PyList_GetItem(multiplicands, i);
   if (PyObject_IsInstance(temp, (PyObject*)&MeanShiftType)!=1)
   {
    PyErr_SetString(PyExc_RuntimeError, "Multiplicand list contains an entity that is not a MeanShift object");
    return NULL;
   }
   
   MeanShift * targ = (MeanShift*)temp;
   if (DataMatrix_features(&targ->dm)!=dims)
   {
    PyErr_SetString(PyExc_RuntimeError, "All the input KDEs must have the same number of features (dimensions)");
    return NULL;
   }
   
   int length = DataMatrix_exemplars(&targ->dm);
   if (length==0)
   {
    PyErr_SetString(PyExc_RuntimeError, "Item in multiplicand list has no exemplars in its KDE");
    return NULL;
   }
   
   if (length>longest) longest = length;
  }
  
  if (PyArray_NDIM(output)!=2)
  {
   PyErr_SetString(PyExc_RuntimeError, "Output array must have two dimensions");
   return NULL;
  }
  
  if (PyArray_DIMS(output)[1]!=dims)
  {
   PyErr_SetString(PyExc_RuntimeError, "Output array must have the same number of colums as the input KDEs have features");
   return NULL; 
  }
  
  if (gibbs<1)
  {
   PyErr_SetString(PyExc_RuntimeError, "gibbs sampling count must be positive");
   return NULL;
  }
  
  if (mci<1)
  {
   PyErr_SetString(PyExc_RuntimeError, "monte carlo integration sampling count must be positive");
   return NULL; 
  }
  
  if (mh<1)
  {
   PyErr_SetString(PyExc_RuntimeError, "Metropolis Hastings proposal count must be positive");
   return NULL;
  }
  
  if ((fake<0)||(fake>2))
  {
   PyErr_SetString(PyExc_RuntimeError, "fake parameter must be 0, 1 or 2");
   return NULL;
  }
  
 // Check for the degenerate situation of only one multiplicand, in which case we can just draw from it to generate the output...
  if (terms==1)
  {
   unsigned int rng_index[3];
   rng_index[0] = rng0;
   rng_index[1] = rng1;
    
   for (rng_index[2]=0; rng_index[2]<PyArray_DIMS(output)[0]; rng_index[2]++)
   {
    float * out = (float*)PyArray_GETPTR2(output, rng_index[2], 0);
    draw(&self->dm, self->kernel, self->config, rng_index, out);
   }
   
   Py_INCREF(Py_None);
   return Py_None;
  }
    
 // Create the MultCache, fill in parameters from the args...
  MultCache mc;
  MultCache_new(&mc);
  
  mc.rng_index[0] = rng0;
  mc.rng_index[1] = rng1;
  
  mc.gibbs_samples = gibbs;
  mc.mci_samples = mci;
  mc.mh_proposals = mh;
  
 // Make sure all the MeanShift objects have a Spatial initialised; create the list of Spatials...
  Spatial * sl = (Spatial)malloc(terms * sizeof(Spatial));
  
  for (i=0; i<terms; i++)
  {
   MeanShift * targ = (MeanShift*)PyList_GetItem(multiplicands, i);
   
   if (targ->spatial==NULL)
   {
    targ->spatial = Spatial_new(targ->spatial_type, &targ->dm);
   }
    
   sl[i] = targ->spatial;
  }
 
 // Call the multiplication method for each draw and let it do the work...
  int * temp1 = (int*)malloc(longest * sizeof(int));
  float * temp2 = (float*)malloc(longest * sizeof(float));
  
  for (i=0; i<PyArray_DIMS(output)[0]; i++)
  {
   float * out = (float*)PyArray_GETPTR2(output, i, 0);
   
   mult(self->kernel, self->config, terms, sl, out, &mc, temp1, temp2, self->quality, fake);
  }
 
 // Clean up the MultCache object and other stuff...
  free(temp1);
  free(temp2);
  free(sl);
  MultCache_delete(&mc);
 
 // Return None...
  Py_INCREF(Py_None);
  return Py_None;
}



static PyMemberDef MeanShift_members[] =
{
 {"quality", T_FLOAT, offsetof(MeanShift, quality), 0, "Value between 0 and 1, inclusive - for kernel types that have an infinite domain this controls how much of that domain to use for the calculations - 0 for lowest quality, 1 for the highest quality. (Ignored by kernel types that have a finite kernel.)"},
 {"epsilon", T_FLOAT, offsetof(MeanShift, epsilon), 0, "For convergance detection - when the step size is smaller than this it stops."},
 {"iter_cap", T_INT, offsetof(MeanShift, iter_cap), 0, "Maximum number of iterations to do before stopping, a hard limit on computation."},
 {"ident_dist", T_FLOAT, offsetof(MeanShift, ident_dist), 0, "If two exemplars are found at any point to have a distance less than this from each other whilst clustering it is assumed they will go to the same destination, saving computation."},
 {"merge_range", T_FLOAT, offsetof(MeanShift, merge_range), 0, "Controls how close two mean shift locations have to be to be merged in the clustering method."},
 {"merge_check_step", T_INT, offsetof(MeanShift, merge_check_step), 0, "When clustering this controls how many mean shift iterations it does between checking for convergance - simply a tradeoff between wasting time doing mean shift when it has already converged and doing proximity checks for convergance. Should only affect runtime."},
 {NULL}
};



static PyMethodDef MeanShift_methods[] =
{
 {"kernels", (PyCFunction)MeanShift_kernels_py, METH_NOARGS | METH_STATIC, "A static method that returns a list of kernel types, as strings."},
 {"get_kernel", (PyCFunction)MeanShift_get_kernel_py, METH_NOARGS, "Returns the string that identifies the current kernel; for complex kernels this may be a complex string containing parameters etc."},
 {"set_kernel", (PyCFunction)MeanShift_set_kernel_py, METH_VARARGS, "Sets the current kernel, as identified by a string. For complex kernels this will probably need to include extra information - e.g. the fisher kernel is given as fisher(alpha) where alpha is a floating point concentration parameter. Note that some kernels (e.g. fisher) take into account the number of features in the data when set - in such cases you must set the kernel type after calling set_data."},
 {"copy_kernel", (PyCFunction)MeanShift_copy_kernel_py, METH_VARARGS, "Given another MeanShift object this copies the settings from it. This is highly recomended when speed matters and you have lots of kernels, as it copies pointers to the internal configuration object and reference counts - for objects with complex configurations this can be an order of magnitude faster. It can also save a lot of memory, via shared caches."},
 
 {"spatials", (PyCFunction)MeanShift_spatials_py, METH_NOARGS | METH_STATIC, "A static method that returns a list of spatial indexing structures you can use, as strings."},
 {"get_spatial", (PyCFunction)MeanShift_get_spatial_py, METH_NOARGS, "Returns the string that identifies the current spatial indexing structure."},
 {"set_spatial", (PyCFunction)MeanShift_set_spatial_py, METH_VARARGS, "Sets the current spatial indexing structure, as identified by a string."},
 
 {"balls", (PyCFunction)MeanShift_balls_py, METH_NOARGS | METH_STATIC, "Returns a list of ball indexing techneques - this is the structure used when clustering to represent the hyper-sphere around the mode that defines a cluster in terms of merging distance."},
 {"get_balls", (PyCFunction)MeanShift_get_balls_py, METH_NOARGS, "Returns the current ball indexing structure, as a string."},
 {"set_balls", (PyCFunction)MeanShift_set_balls_py, METH_VARARGS, "Sets the current ball indexing structure, as identified by a string."},
 
 {"info", (PyCFunction)MeanShift_info_py, METH_VARARGS | METH_STATIC, "A static method that is given the name of a kernel, spatial or ball. It then returns a human readable description of that entity."},
 {"info_config", (PyCFunction)MeanShift_info_config_py, METH_VARARGS | METH_STATIC, "Given the name of a kernel this returns None if the kernel does not require any configuration, or a string describing how to configure it if it does."},
 
 {"set_data", (PyCFunction)MeanShift_set_data_py, METH_VARARGS, "Sets the data matrix, which defines the probability distribution via a kernel density estimate that everything is using. The data matrix is used directly, so it should not be modified during use as it could break the data structures created to accelerate question answering. First parameter is a numpy matrix (Any normal numerical type), the second a string with its length matching the number of dimensions of the matrix. The characters in the string define the meaning of each dimension: 'd' (data) - changing the index into this dimension changes which exemplar you are indexing; 'f' (feature) - changing the index into this dimension changes which feature you are indexing; 'b' (both) - same as d, except it also contributes an item to the feature vector, which is essentially the position in that dimension (used on the dimensions of an image for instance, to include pixel position in the feature vector). The system unwraps all data indices and all feature indices in row major order to hallucinate a standard data matrix, with all 'both' features at the start of the feature vector. Note that calling this resets scale. A third optional parameter sets an index into the original feature vector (Including the dual dimensions, so you can use one of them to provide weight) that is to be the weight of the feature vector - this effectivly reduces the length of the feature vector, as used by all other methods, by one."}, 
 {"get_dm", (PyCFunction)MeanShift_get_dm_py, METH_NOARGS, "Returns the current data matrix, which will be some kind of numpy ndarray"},
 {"get_dim", (PyCFunction)MeanShift_get_dim_py, METH_NOARGS, "Returns the string that gives the meaning of each dimension, as matched to the number of dimensions in the data matrix."},
 {"get_weight_dim", (PyCFunction)MeanShift_get_weight_dim_py, METH_NOARGS, "Returns the feature vector index that provides the weight of each sample, or None if there is not one and they are all fixed to 1."},
 
 {"set_scale", (PyCFunction)MeanShift_set_scale_py, METH_VARARGS, "Given two parameters. First is an array indexed by feature to get a multiplier that is applied before the kernel (Which is always of radius 1, or some approximation of.) is considered - effectivly an inverse bandwidth in kernel density estimation terms. Second is an optional scale for the weight assigned to each feature vector via the set_data method (In the event that no weight is assigned this parameter is the weight of each feature vector, as the default is 1)."},
 {"get_scale", (PyCFunction)MeanShift_get_scale_py, METH_NOARGS, "Returns a copy of the scale array (Inverse bandwidth)."},
 {"get_weight_scale", (PyCFunction)MeanShift_get_weight_scale_py, METH_NOARGS, "Returns the scalar for the weight of each sample - typically left as 1."},
 
 {"exemplars", (PyCFunction)MeanShift_exemplars_py, METH_NOARGS, "Returns how many exemplars are in the hallucinated data matrix."},
 {"features", (PyCFunction)MeanShift_features_py, METH_NOARGS, "Returns how many features are in the hallucinated data matrix."},
 {"weight", (PyCFunction)MeanShift_weight_py, METH_NOARGS, "Returns the total weight of the included data, taking into account the weight channel if provided."},
 {"stats", (PyCFunction)MeanShift_stats_py, METH_NOARGS, "Returns some basic stats about the data set - (mean, standard deviation). These are per channel."},

 {"scale_silverman", (PyCFunction)MeanShift_scale_silverman_py, METH_NOARGS, "Sets the scale for the current data using Silverman's rule of thumb, generalised to multidimensional data (Multidimensional version often attributed to Wand & Jones.). Note that this is assuming you are using Gaussian kernels and that the samples have been drawn from a Gaussian - if these asumptions are valid you should probably just fit a Gaussian in the first place, if they are not you should not use this method. Basically, do not use!"},
 {"scale_scott", (PyCFunction)MeanShift_scale_scott_py, METH_NOARGS, "Alternative to scale_silverman - assumptions are very similar and it is hence similarly crap - would recomend against this, though maybe prefered to Silverman."},
 {"loo_nll", (PyCFunction)MeanShift_loo_nll_py, METH_VARARGS, "Calculate the negative log liklihood of the model where it leaves out the sample whos probability is being calculated and then muliplies together the probability of all samples calculated independently. This can be used for model comparison, to see which is better out of several configurations, be that kernel size, kernel type etc. Takes one optional parameter, which is a lower bound on probability, to avoid outliers causing problems - defaults to 1e-16"},
 
 {"prob", (PyCFunction)MeanShift_prob_py, METH_VARARGS, "Given a feature vector returns its probability, as calculated by the kernel density estimate that is defined by the data and kernel. Be warned that the return value can be zero."},
 {"probs", (PyCFunction)MeanShift_probs_py, METH_VARARGS, "Given a data matrix returns an array (1D) containing the probability of each feature, as calculated by the kernel density estimate that is defined by the data and kernel. Be warned that the return value can be zero."},
 
 {"draw", (PyCFunction)MeanShift_draw_py, METH_VARARGS, "Allows you to draw from the distribution represented by the kernel density estimate. It is actually entirly deterministic - you hand over three unsigned 32 bit integers which index into the rng, so you should iterate them to get a sequence. (Second two rng indices are optional, and default to 0.) Returns a vector."},
 {"draws", (PyCFunction)MeanShift_draws_py, METH_VARARGS, "Allows you to draw from the distribution represented by the kernel density estimate. Same as draw except it returns a matrix - the first number handed in is how many draws to make, the next two indices going into the Philox rng. The same as calling the draw method with the first two rng indices set as passed in and the third set to 0 then 1, 2 etc. (Second index is optional and defaults to 0 if not provided.) Returns an array, <# draws>X<# exemplars>."},
 {"bootstrap", (PyCFunction)MeanShift_bootstrap_py, METH_VARARGS, "Does a bootstrap draw from the samples - essentially the same as draws but assuming a Dirac delta function for the kernel. You provide the number of draws as the first parameter, then 3 rng indexing parameters, that make it deterministic (Last two are optional - default to 0). Returns an array, <# draws>X<# exemplars>."},
 
 {"mode", (PyCFunction)MeanShift_mode_py, METH_VARARGS, "Given a feature vector returns its mode as calculated using mean shift - essentially the maxima in the kernel density estimate to which you converge by climbing the gradient."},
 {"modes", (PyCFunction)MeanShift_modes_py, METH_VARARGS, "Given a data matrix [exemplar, feature] returns a matrix of the same size, where each feature has been replaced by its mode, as calculated using mean shift."},
 {"modes_data", (PyCFunction)MeanShift_modes_data_py, METH_NOARGS, "Runs mean shift on the contained data set, returning a feature vector for each data point. The return value will be indexed in the same way as the provided data matrix, but without the feature dimensions, with an extra dimension at the end to index features. Note that the resulting output will contain a lot of effective duplication, making this a very inefficient method - your better off using the cluster method."},
 
 {"cluster", (PyCFunction)MeanShift_cluster_py, METH_NOARGS, "Clusters the exemplars provided by the data matrix - returns a two tuple (data matrix of all the modes in the dataset, indexed [mode, feature], A matrix of integers, indicating which mode each one has been assigned to by indexing the mode array. Indexing of this array is identical to the provided data matrix, with any feature dimensions removed.). The clustering is replaced each time this is called - do not expect cluster indices to remain consistant after calling this."},
 {"assign_cluster", (PyCFunction)MeanShift_assign_cluster_py, METH_VARARGS, "After the cluster method has been called this can be called with a single feature vector. It will then return the index of the cluster to which it has been assigned, noting that this will map to the mode array returned by the cluster method. In the event it does not map to a pre-existing cluster it will return a negative integer - this usually means it is so far from the provided data that the kernel does not include any samples."},
 {"assign_clusters", (PyCFunction)MeanShift_assign_clusters_py, METH_VARARGS, "After the cluster method has been called this can be called with a data matrix. It will then return the indices of the clusters to which each feature vector has been assigned, as a 1D numpy array, noting that this will map to the mode array returned by the cluster method. In the event any entry does not map to a pre-existing cluster it will return a negative integer for it - this usually means it is so far from the provided data that the kernel does not include any samples."},
 
 {"manifold", (PyCFunction)MeanShift_manifold_py, METH_VARARGS, "Given a feature vector and the dimensionality of the manifold projects the feature vector onto the manfold using subspace constrained mean shift. Returns an array with the same shape as the input. A further optional boolean parameter allows you to enable calculation of the hessain for every iteration (The default, True, correct algorithm), or only do it once at the start (False, incorrect but works for clean data.)."},
 {"manifolds", (PyCFunction)MeanShift_manifolds_py, METH_VARARGS, "Given a data matrix [exemplar, feature] and the dimensionality of the manifold projects the feature vectors onto the manfold using subspace constrained mean shift. Returns a data matrix with the same shape as the input. A further optional boolean parameter allows you to enable calculation of the hessain for every iteration (The default, True, correct algorithm), or only do it once at the start (False, incorrect but works for clean data.)."},
 {"manifolds_data", (PyCFunction)MeanShift_manifolds_data_py, METH_VARARGS, "Given the dimensionality of the manifold projects the feature vectors that are defining the density estimate onto the manfold using subspace constrained mean shift. The return value will be indexed in the same way as the provided data matrix, but without the feature dimensions, with an extra dimension at the end to index features. A further optional boolean parameter allows you to enable calculation of the hessain for every iteration (The default, True, correct algorithm), or only do it once at the start (False, incorrect but works for clean data.)."},
 
 {"mult", (PyCFunction)MeanShift_mult_py, METH_KEYWORDS | METH_VARARGS | METH_STATIC, "A static method that allows you to multiply a bunch of kernel density estimates, and draw some samples from the resulting distribution, outputing the samples into an array. The first input must be a list of MeanShift objects (At least of length 1, though if length 1 it just resamples the input), the second a numpy array for the output - it must be 2D and have the same number of columns as all the MeanShift objects have features/dims. Its row count is how many samples will be drawn from the distribution implied by multiplying the KDEs together. Note that the first object in the MeanShift object list gets to set the kernel - it is assumed that all further objects have the same kernel, though if they don't it will still run through under that assumption just fine. Further to the first two inputs dictionary parameters it allows parameters to be set by name: {'rng0': Controls the deterministic random number generator, 'rng1': Ditto, 'gibbs': Number of Gibbs samples to do, noting its multiplied by the length of the multiplication list and is the number of complete passes through the state, 'mci': Number of samples to do if it has to do monte carlo integration, 'mh': Number of Metropolis-Hastings steps it will do if it has to, multiplied by the length of the multiplicand list, 'fake': Allows you to request an incorrect-but-useful result - the default of 0 is the correct output, 1 is a mode from the Gibbs sampled mixture component instead of a draw, whilst 2 is the average position of the components that made up the selected mixture component.}"},
 
 {NULL}
};



static PyTypeObject MeanShiftType =
{
 PyObject_HEAD_INIT(NULL)
 0,                                /*ob_size*/
 "ms_c.MeanShift",                 /*tp_name*/
 sizeof(MeanShift),                /*tp_basicsize*/
 0,                                /*tp_itemsize*/
 (destructor)MeanShift_dealloc_py, /*tp_dealloc*/
 0,                                /*tp_print*/
 0,                                /*tp_getattr*/
 0,                                /*tp_setattr*/
 0,                                /*tp_compare*/
 0,                                /*tp_repr*/
 0,                                /*tp_as_number*/
 0,                                /*tp_as_sequence*/
 0,                                /*tp_as_mapping*/
 0,                                /*tp_hash */
 0,                                /*tp_call*/
 0,                                /*tp_str*/
 0,                                /*tp_getattro*/
 0,                                /*tp_setattro*/
 0,                                /*tp_as_buffer*/
 Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
 "An object implimenting mean shift; also includes kernel density estimation and subspace constrained mean shift using the same object, such that they are all using the same underlying density estimate. Includes multiple spatial indexing schemes and kernel types, including one for directional data. Clustering is supported, with a choice of cluster intersection tests, as well as the ability to interpret exemplar indexing dimensions of the data matrix as extra features, so it can handle the traditional image segmentation scenario.", /* tp_doc */
 0,                                /* tp_traverse */
 0,                                /* tp_clear */
 0,                                /* tp_richcompare */
 0,                                /* tp_weaklistoffset */
 0,                                /* tp_iter */
 0,                                /* tp_iternext */
 MeanShift_methods,                /* tp_methods */
 MeanShift_members,                /* tp_members */
 0,                                /* tp_getset */
 0,                                /* tp_base */
 0,                                /* tp_dict */
 0,                                /* tp_descr_get */
 0,                                /* tp_descr_set */
 0,                                /* tp_dictoffset */
 0,                                /* tp_init */
 0,                                /* tp_alloc */
 MeanShift_new_py,                 /* tp_new */
};



static PyMethodDef ms_c_methods[] =
{
 {NULL}
};



//#include "bessel.h"



#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC initms_c(void)
{
 PyObject * mod = Py_InitModule3("ms_c", ms_c_methods, "Primarily provides a mean shift implementation, but also includes kernel density estimation and subspace constrained mean shift using the same object, such that they are all using the same underlying density estimate. Includes multiple spatial indexing schemes and kernel types, including support for directional data. Clustering is supported, with a choice of cluster intersection tests, as well as the ability to interpret exemplar indexing dimensions of the data matrix as extra features, so it can handle the traditional image segmentation scenario efficiently. Exemplars can also be weighted.");
 
 import_array();
 
 if (PyType_Ready(&MeanShiftType) < 0) return;
 
 //int order, x;
 //for (order=0; order<7; order++)
 //{
 // for (x=0; x<9; x++)
 // {
 //  printf("modified bessel function , first kind (order=%f,x=%f) = %f = %f\n", (float)(0.5*order), (float)x, ModBesselFirst(order, x, 1e-6, 1024), exp(LogModBesselFirst(order, x, 1e-6, 1024)));
 // }
 //}
 //printf("modified bessel function , first kind (order=3,x=48) = %f = %f\n", //ModBesselFirst(6, 48.0, 1e-6, 1024), exp(LogModBesselFirst(6, 48.0, 1e-6, 1024)));
 
 //int x2;
 //for (x2=0; x2<9; x2++)
 //{
 // printf("log gamma (%f) = %f\n", 0.5*x2, LogGamma(x2));
 //}
 
 Py_INCREF(&MeanShiftType);
 PyModule_AddObject(mod, "MeanShift", (PyObject*)&MeanShiftType);
}
